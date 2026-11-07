package com.netmaru.rea

import android.content.SharedPreferences
import android.inputmethodservice.InputMethodService
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import com.netmaru.rea.engine.Engines
import com.netmaru.rea.engine.HangulComposer
import com.netmaru.rea.engine.InputSession
import com.netmaru.rea.engine.KoreanModel
import com.netmaru.rea.engine.SplittingEngine
import com.netmaru.rea.japanese.MozcConverter
import java.io.File
import kotlin.concurrent.thread
import com.netmaru.rea.engine.Language

/** What the keys type; shift and 123 are the keyboard's own. */
sealed interface Key {
    data class Char(val char: kotlin.Char) : Key
    data object Space : Key
    data object Delete : Key
    data object Return : Key
    /** 한あA: the word just typed in the next language. */
    data object Cycle : Key
}

/** How far toward a neighbor key (0 the typed key's center, 1 the neighbor's) a touch must lie for
 *  the neighbor's word to be offered when the typed word is fine: past a third of the way. */
private const val NEAR = 0.33f

/** A word in the bar above the keys; `selected` is highlighted (the one Space/Return takes). */
data class Candidate(val text: String, val selected: Boolean = false)

/** The engine behind the keys (iOS KeyboardViewController's tap/refreshCandidates/replaceWord):
 *  an InputSession typing through a ConnectionClient, plus autocorrect and the candidate bar. */
class Typist(private val service: InputMethodService) {
    private val client = ConnectionClient { service.currentInputConnection }
    private lateinit var session: InputSession
    /** The settings the session was built for. */
    private var built: String? = null
    /** Kept across rebuilds; its dictionary is memory-mapped, loaded off the main thread. */
    private var mozc: MozcConverter? = null
    private val splitter get() = session.engine.base as SplittingEngine
    private var suggestions = Suggestions()
    private var actions = emptyList<() -> Unit>()
    var candidates = emptyList<Candidate>()
        private set
    /** Told when [candidates] change after a key (the bar). */
    var onCandidates: () -> Unit = {}
    /** A key came after the candidates were made: they're for an earlier word. */
    private var stale = false
    private val handler = Handler(Looper.getMainLooper())
    private val refreshLater = Runnable { refresh(); onCandidates() }

    /** Where each key of the word being typed was touched (dp), for nearby-key suggestions and
     *  touch learning; null for a key not touched (typed by the app, a picked candidate). */
    private val touches = mutableListOf<Pair<Float, Float>?>()
    /** The keys [touches] are for (the session's keys when they were last matched). */
    private var touchKeys = ""

    /** Matches [touches] to the session's keys after a key: a letter added keeps them and adds its
     *  touch; the engine keeping only the end (a Japanese prefix committed, Korean typed on) keeps
     *  the end's; anything else keeps the touches of the keys still in front and knows no others
     *  (a respelled key, a key the engine made). */
    private fun follow(before: String, typed: Char?, touch: Pair<Float, Float>?) {
        val after = if (built != null) session.keys else ""
        val withKey = if (typed != null) before + typed else before
        val kept: List<Pair<Float, Float>?> = when {
            after.isEmpty() -> emptyList()
            typed != null && after == withKey -> touches + touch
            typed != null && withKey.endsWith(after) -> (touches + touch).takeLast(after.length)
            else -> {
                val same = before.commonPrefixWith(after).length
                touches.take(same) + List(after.length - same) { null }
            }
        }
        touches.clear(); touches.addAll(kept)
        touchKeys = after
    }

    /** The word ended (committed, replaced, the cursor moved): its touches go with it. */
    private fun forgetTouches() { touches.clear(); touchKeys = "" }
    /** A key's center on the keyboard (dp), set by the keyboard. */
    var keyCenter: (Char) -> Pair<Float, Float>? = { null }
    /** A nearby-key fix was picked: key `n` was meant, touched (dx, dy) from its center. */
    var onLearn: (n: Char, dx: Float, dy: Float) -> Unit = { _, _, _ -> }
    /** Nearby-key suggestions for a word that is itself fine (settings), and touch learning (off in
     *  password fields and fields that ask for no learning). */
    var nearbyKeys = true
    var learning = true
    /** Text shortcuts from the settings: typed keys → the phrase offered for them. */
    private var shortcuts = emptyMap<String, String>()

    /** The keyboard comes up: a new field. Rebuilds the session if the settings changed in the app. */
    fun start(prefs: SharedPreferences) {
        shortcuts = Prefs.shortcuts(prefs).toMap()
        val order = Prefs.order(prefs)
        val options = Prefs.options.map { (key, _, default) -> prefs.getBoolean(key, default) }
        val settings = "$order $options"
        if (settings != built) {
            built = settings
            val converter = if (Language.JAPANESE in order) {
                // codeCacheDir: the unpacked dictionary is dropped when the app (and its dictionary) updates
                mozc ?: MozcConverter(learningFile = File(service.filesDir, "mozc-learning.txt"), unpacked = File(service.codeCacheDir, "mozc.dic"))
                    .also { mozc = it; thread { runCatching(it::load) } }
            } else null
            session = Engines.makeSession(order, converter, liveConversion = options[3], returnConfirmsOnly = options[0],
                fullWidthDigits = options[1], fullWidthSpace = options[2], learningFile = File(service.filesDir, "learning.txt"))
            thread { KoreanModel.warmUp() }
        }
        forgetTouches()
        session.activate()
        refresh()
    }

    /** The field is going away: the word stays as typed. */
    fun finish() {
        forgetTouches()
        if (built == null) return
        session.commit(client)
        runCatching { mozc?.save() } // a full disk loses the learning, not the keyboard
        refresh()
    }

    /** The app moved the cursor (or the text changed under it): the next key starts afresh there.
     *  Whether that changed the candidates. */
    fun selectionChanged(selecting: Boolean = false): Boolean {
        if (built == null || !client.resetIfMovedAway(selecting)) return false
        forgetTouches()
        session.commitForNewContext(null)
        refresh()
        return true
    }

    /** The text changes now, in one batch; the candidates once typing pauses. [touch]: where a
     *  letter key was touched, in dp. */
    fun tap(key: Key, touch: Pair<Float, Float>? = null) {
        val before = touchKeys
        client.batch { type(key) }
        follow(before, (key as? Key.Char)?.char, touch)
        refreshSoon()
    }

    /** The word's keys now (before a letter, for [replaceLastKey]). */
    val keysNow get() = if (built != null) session.keys else ""

    /** A letter swiped up: it becomes [symbol]. Delete takes it back; where Delete took more (a
     *  kana: か for its a), the keys before it are typed again first. */
    fun replaceLastKey(before: String, symbol: Char) {
        tap(Key.Delete)
        val now = keysNow
        if (before.startsWith(now)) for (c in before.substring(now.length)) tap(Key.Char(c))
        tap(Key.Char(symbol))
    }

    /** Space dragged sideways: the word goes in as it is, then the cursor moves [steps] characters
     *  (negative: left), as on Gboard. */
    fun moveCursor(steps: Int) {
        if (built != null && session.keys.isNotEmpty()) { session.commit(client); forgetTouches(); refresh(); onCandidates() }
        val code = if (steps < 0) android.view.KeyEvent.KEYCODE_DPAD_LEFT else android.view.KeyEvent.KEYCODE_DPAD_RIGHT
        repeat(kotlin.math.abs(steps)) { service.sendDownUpKeyEvents(code) }
    }

    /** How far (0 at key `t`'s center, 1 at `n`'s) the touch for the key at [i] lay toward `n`. */
    private fun toward(i: Int, n: Char, keys: String): Float? {
        val p = touches.getOrNull(i) ?: return null
        val t = keyCenter(keys[i].lowercaseChar()) ?: return null
        val c = keyCenter(n.lowercaseChar()) ?: return null
        val vx = c.first - t.first; val vy = c.second - t.second
        return ((p.first - t.first) * vx + (p.second - t.second) * vy) / (vx * vx + vy * vy)
    }

    /** The key a fix swapped (its index and new key), if it swapped exactly one. */
    private fun swapped(typed: String, fix: String): Pair<Int, Char>? {
        if (typed.length != fix.length) return null
        val diff = typed.indices.filter { typed[it] != fix[it] }
        return diff.singleOrNull()?.let { it to fix[it] }
    }

    /** A picked fix that swapped one key: that key was meant where it was touched. */
    private fun learn(typed: String, fixKeys: String?, touchesThen: List<Pair<Float, Float>?>) {
        if (!learning || fixKeys == null) return
        val (i, n) = swapped(typed, fixKeys) ?: return
        val p = touchesThen.getOrNull(i) ?: return
        val c = keyCenter(n.lowercaseChar()) ?: return
        onLearn(n.lowercaseChar(), p.first - c.first, p.second - c.second)
    }

    private fun type(key: Key) {
        val engineKey = when (key) {
            is Key.Char -> if (key.char.isLetter()) InputSession.Key.Letter(key.char) else InputSession.Key.Printable(key.char)
            Key.Delete -> InputSession.Key.Delete
            Key.Cycle -> InputSession.Key.Cycle
            Key.Space -> {
                autocorrect()
                InputSession.Key.Printable(' ')
            }
            Key.Return -> {
                // Japanese being typed: return takes the first candidate (highlighted), like the system keyboard.
                session.typingCandidates.firstOrNull()?.let {
                    session.choose(it, client)
                    return
                }
                InputSession.Key.Enter
            }
        }
        if (!session.handle(engineKey, client)) passToApp(engineKey)
    }

    fun choose(index: Int) {
        if (stale) return refresh() // tapped a bar that's about to change
        actions.getOrNull(index)?.invoke()
        follow(touchKeys, null, null) // a candidate may have committed the word, or kept some of it
        refresh()
    }

    /** Space after a typo: the fix goes in first. Kana with romaji left over (てh) goes in as
     *  English with its fix too; a word straight after . / @ : (README.md, a URL) isn't corrected. */
    private fun autocorrect() {
        if (stale) refresh()
        val marked = client.marked
        val leftover = session.lastTrace?.decision?.language == Language.JAPANESE && marked.any { it.code < 128 }
        val attached = client.before()?.dropLast(marked.length)?.lastOrNull()?.let { !it.isLetterOrDigit() && !it.isWhitespace() } ?: false
        if (attached) return
        val fix = suggestions.autocorrect ?: if (leftover) Suggestions.english(session.keys, splitter.english).autocorrect else null
        fix?.let(::replaceWord)
    }

    /** What the app does with a key the session didn't take. */
    private fun passToApp(key: InputSession.Key) {
        val ic = service.currentInputConnection ?: return
        when (key) {
            is InputSession.Key.Letter -> ic.commitText(key.ch.toString(), 1)
            is InputSession.Key.Printable -> ic.commitText(key.ch.toString(), 1)
            InputSession.Key.Delete ->
                if (ic.getSelectedText(0).isNullOrEmpty()) ic.deleteSurroundingTextInCodePoints(1, 0) else ic.commitText("", 1)
            // the field's action (Go, Send…) if it has one, else a new line
            InputSession.Key.Enter -> if (!service.sendDefaultEditorAction(true)) service.sendKeyChar('\n')
            else -> {}
        }
    }

    /** Candidates once keys stop for 80ms (HeliBoard waits 100): a fast run of keys only types.
     *  On the main thread, as the session isn't thread-safe; a newer key cancels the wait. */
    private fun refreshSoon() {
        stale = true
        handler.removeCallbacks(refreshLater)
        handler.postDelayed(refreshLater, 80)
    }

    /** Kanji candidates while typing Japanese or converting (the selected one highlighted),
     *  else Korean/English fixes for the word: the word as typed first, then the fixes. */
    private fun refresh() {
        val start = SystemClock.elapsedRealtimeNanos()
        handler.removeCallbacks(refreshLater)
        stale = false
        val list = mutableListOf<Pair<Candidate, () -> Unit>>()
        suggestions = Suggestions()
        if (built != null) {
            val conversion = session.conversion
            val kanji = conversion?.candidates ?: session.typingCandidates.take(30)
            kanji.forEachIndexed { index, candidate ->
                list += Candidate(candidate.text, index == (conversion?.selected ?: 0)) to {
                    if (conversion != null) session.choose(index, client) else session.choose(candidate, client)
                }
            }
            // a text shortcut typed (brb): its phrase first, put in with a space when picked
            shortcuts[session.keys.lowercase()]?.let { phrase ->
                list.add(0, Candidate(phrase) to { replaceWord(phrase); client.insertText(" ", null) })
            }
            val word = client.marked
            val trace = session.lastTrace
            if (kanji.isEmpty() && word.isNotEmpty() && trace != null && trace.keys == session.keys) {
                var typed = word
                when (trace.decision.language) {
                    // the word in the language decided (the preview may still show another: てh for teh)
                    Language.KOREAN -> {
                        typed = HangulComposer.compose(session.keys)?.text ?: word
                        suggestions = Suggestions.korean(typed, session.keys, splitter.korean)
                    }
                    Language.ENGLISH -> {
                        typed = session.keys
                        suggestions = Suggestions.english(typed, splitter.english)
                    }
                    else -> {}
                }
                val keys = session.keys
                if (suggestions.words.isEmpty() && nearbyKeys) {
                    // a fine word, but a touch near a neighbor key: what that key would have made
                    suggestions = when (trace.decision.language) {
                        Language.KOREAN -> Suggestions.nearby(keys, splitter.korean, null) { i, n -> (toward(i, n, keys) ?: 0f) >= NEAR }
                        Language.ENGLISH -> Suggestions.nearby(keys, null, splitter.english) { i, n -> (toward(i, n, keys) ?: 0f) >= NEAR }
                        else -> Suggestions()
                    }
                }
                if (suggestions.words.isNotEmpty()) {
                    // the fix Space puts in first, then the others by how close the touch was to the key they swap in
                    val auto = suggestions.autocorrect
                    val others = suggestions.words.filter { it != auto }.sortedByDescending { w ->
                        suggestions.keys[w]?.let { swapped(keys, it) }?.let { (i, n) -> toward(i, n, keys) } ?: -1f
                    }
                    val touchesNow = touches.toList()
                    val keysOf = suggestions.keys
                    // a tap puts the word in with a space
                    list += Candidate("“$typed”") to { replaceWord(typed); client.insertText(" ", null) }
                    for (s in listOfNotNull(auto) + others) list += Candidate(s, s == auto) to {
                        learn(keys, keysOf[s], touchesNow)
                        replaceWord(s); client.insertText(" ", null)
                    }
                }
            }
        }
        candidates = list.map { it.first }
        actions = list.map { it.second }
        if (TRACE) Log.d("REA", "candidates ${list.size} ${(SystemClock.elapsedRealtimeNanos() - start) / 1000}us")
    }

    /** Finish the session, replacing only the differing end of the displayed word. */
    private fun replaceWord(text: String) {
        if (client.marked.isEmpty() || client.before()?.endsWith(client.marked) != true) return
        forgetTouches()
        session.commit(null) // finish without replacing the displayed word with a new decision
        session.commit(null) // and close the 한あA window on the word as typed
        client.insertText(text, null)
    }
}
