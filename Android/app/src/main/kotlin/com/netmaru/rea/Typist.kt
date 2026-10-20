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

    /** The keyboard comes up: a new field. Rebuilds the session if the settings changed in the app. */
    fun start(prefs: SharedPreferences) {
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
        }
        session.activate()
        refresh()
    }

    /** The field is going away: the word stays as typed. */
    fun finish() {
        if (built == null) return
        session.commit(client)
        runCatching { mozc?.save() } // a full disk loses the learning, not the keyboard
        refresh()
    }

    /** The app moved the cursor (or the text changed under it): the next key starts afresh there.
     *  Whether that changed the candidates. */
    fun selectionChanged(): Boolean {
        if (built == null || !client.resetIfMovedAway()) return false
        session.commitForNewContext(null)
        refresh()
        return true
    }

    /** The text changes now, in one batch; the candidates once typing pauses. */
    fun tap(key: Key) {
        client.batch { type(key) }
        refreshSoon()
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
        refresh()
    }

    /** A touch near a neighbor key mid-word: the neighbor wins when it makes the word much more
     *  likely (fgem → fhem, 로드), as on iOS. ponytail: one key at a time, a fixed margin. */
    fun corrected(typed: Char, neighbor: Char): Char {
        val keys = session.keys
        if (keys.isEmpty()) return typed
        return if (plausibility(keys + neighbor) > plausibility(keys + typed) + 1.0) neighbor else typed
    }

    /** How likely the keys are as a word in any enabled language (average log-probability per key). */
    private fun plausibility(keys: String) = listOfNotNull(
        splitter.english.averageLogProbability(keys.lowercase()),
        splitter.korean.score(keys, partial = true)?.averageLogProbability,
        splitter.japanese?.japanese?.score(keys, partial = true)?.averageLogProbability,
    ).maxOrNull() ?: Double.NEGATIVE_INFINITY

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
                if (suggestions.words.isNotEmpty()) {
                    // a tap puts the word in with a space
                    list += Candidate("“$typed”") to { replaceWord(typed); client.insertText(" ", null) }
                    for (s in suggestions.words) list += Candidate(s, s == suggestions.autocorrect) to { replaceWord(s); client.insertText(" ", null) }
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
        session.commit(null) // finish without replacing the displayed word with a new decision
        session.commit(null) // and close the 한あA window on the word as typed
        client.insertText(text, null)
    }
}
