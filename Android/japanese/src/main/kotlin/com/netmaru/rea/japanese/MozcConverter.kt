package com.netmaru.rea.japanese

import com.netmaru.rea.engine.ConversionCandidate
import com.netmaru.rea.engine.JapaneseConverter
import java.io.DataInputStream
import java.io.File
import java.io.InputStream
import java.nio.ByteBuffer
import java.util.PriorityQueue
import java.util.zip.GZIPInputStream

/**
 * Kana → kanji over Mozc's OSS dictionary (tools/japanese-dict/build.py): a lattice of dictionary
 * words, Viterbi with word and connection costs, N-best by backward A*.
 *
 * [dictionary] opens mozc.dic.gz (the bundled resource by default; an Android asset works too).
 * It is read once, on the first [candidates] call (~0.15 s and ~55 MB of heap on a laptop JVM).
 * [learningFile] keeps committed words across launches ("reading\tsurface\tcount" lines); null = memory only.
 */
class MozcConverter(
    private val dictionary: () -> InputStream = { MozcConverter::class.java.getResourceAsStream("/mozc.dic.gz")!! },
    private val learningFile: File? = null,
) : JapaneseConverter {
    private val dict by lazy { dictionary().use(::Dictionary) }

    /** Learned "reading\tsurface" → times committed. */
    private val learned: MutableMap<String, Int> by lazy {
        learningFile?.takeIf { it.exists() }?.readLines()?.mapNotNull { line ->
            val parts = line.split('\t')
            parts.getOrNull(2)?.toIntOrNull()?.let { "${parts[0]}\t${parts[1]}" to it }
        }?.toMap(HashMap()) ?: HashMap()
    }

    private var lastKana = ""
    private var lastPaths = mapOf<String, List<Node>>()

    override fun candidates(kana: String): List<ConversionCandidate> {
        if (kana.isEmpty()) return emptyList()
        val lattice = Lattice(kana)
        val paths = LinkedHashMap<String, List<Node>>()
        val out = mutableListOf<ConversionCandidate>()
        fun add(path: List<Node>) {
            val text = path.joinToString("") { it.surface }
            if (paths.putIfAbsent(text, path) == null) out += ConversionCandidate(text, path.last().end)
        }
        lattice.nBest(10).forEach(::add)
        lattice.firstClauses().take(10).forEach(::add)
        for (plain in listOf(kana, katakana(kana))) {
            if (plain !in paths) out += ConversionCandidate(plain, kana.length)
        }
        lastKana = kana
        lastPaths = paths
        return out
    }

    override fun learn(candidate: ConversionCandidate, kana: String) {
        if (kana != lastKana) candidates(kana)
        for (node in lastPaths[candidate.text] ?: return) {
            if (node.lid == 0 || dict.functional[node.lid]) continue // passed-through character, particle
            val key = "${node.reading}\t${node.surface}"
            learned.keys.removeAll { it.startsWith("${node.reading}\t") && it != key } // the latest choice wins
            learned[key] = (learned[key] ?: 0) + 1
        }
    }

    override fun save() {
        val file = learningFile ?: return
        val temp = File(file.path + ".tmp") // a crash mid-write keeps the old file
        temp.writeText(learned.entries.joinToString("") { "${it.key}\t${it.value}\n" })
        temp.renameTo(file)
    }

    private class Node(val start: Int, val end: Int, val reading: String, val surface: String, val lid: Int, val rid: Int, val cost: Int) {
        var forward = 0 // best cost from BOS through this node
        var backward = 0 // best cost from after this node to EOS
        var next: Node? = null // the node after this one on that best path
    }

    private inner class Lattice(val kana: String) {
        val n = kana.length
        val bos = Node(0, 0, "", "", 0, 0, 0)
        val eos = Node(n, n, "", "", 0, 0, 0)
        val endsAt = Array(n + 1) { mutableListOf<Node>() }
        val startsAt = Array(n + 1) { mutableListOf<Node>() }

        init {
            endsAt[0] += bos
            startsAt[n] += eos
            var limit = 0 // end of the kana run at i: words don't cross punctuation
            for (i in 0 until n) {
                if (limit <= i) limit = (i until n).firstOrNull { !isKana(kana[it]) } ?: n
                if (!isKana(kana[i])) { // punctuation and the like pass through, as at a sentence boundary
                    link(Node(i, i + 1, kana.substring(i, i + 1), kana.substring(i, i + 1), 0, 0, 0))
                    continue
                }
                var found = false
                dict.lookup(kana, i, limit) { end, surface, lid, rid, cost ->
                    val reading = kana.substring(i, end)
                    val boost = learned["$reading\t${surface ?: reading}"]?.let { 1000 * minOf(it, 4) } ?: 0
                    val kanaOnly = if (surface == null) 1 else 0 // ties (よし/良し) go to the written form
                    link(Node(i, end, reading, surface ?: reading, lid, rid, cost + kanaOnly - boost))
                    found = true
                }
                if (!found) link(Node(i, i + 1, kana.substring(i, i + 1), kana.substring(i, i + 1), 0, 0, UNKNOWN_COST))
            }
            // Forward Viterbi (nodes by start), then backward (by end).
            for (i in 0..n) for (node in startsAt[i]) {
                // Nothing ends where node starts (no 1-kana word before it): unreachable.
                node.forward = (endsAt[node.start].minOfOrNull { it.forward + dict.connection(it.rid, node.lid) } ?: UNREACHABLE) + node.cost
            }
            for (i in n downTo 0) for (node in endsAt[i]) {
                if (node === eos) continue
                val best = startsAt[node.end].minBy { dict.connection(node.rid, it.lid) + it.cost + it.backward }
                node.next = best
                node.backward = dict.connection(node.rid, best.lid) + best.cost + best.backward
            }
        }

        fun link(node: Node) {
            startsAt[node.start] += node
            endsAt[node.end] += node
        }

        /** Whole-reading conversions, best first: A* from EOS back to BOS, the forward costs as the exact heuristic. */
        fun nBest(count: Int): List<List<Node>> {
            class Partial(val node: Node, val cost: Int, val after: Partial?) // cost: everything after node
            val queue = PriorityQueue<Partial>(compareBy { it.node.forward + it.cost })
            queue += Partial(eos, 0, null)
            val out = mutableListOf<List<Node>>()
            val seen = HashSet<String>()
            var pops = 0
            while (out.size < count && queue.isNotEmpty() && pops++ < 5000) {
                val p = queue.poll()
                if (p.node === bos) {
                    val path = generateSequence(p.after) { it.after }.map { it.node }.filter { it !== eos }.toList()
                    if (seen.add(path.joinToString("") { it.surface })) out += path
                    continue
                }
                for (prev in endsAt[p.node.start]) {
                    queue += Partial(prev, p.cost + p.node.cost + dict.connection(prev.rid, p.node.lid), p)
                }
            }
            return out
        }

        /** The first word plus the particles after it on its best path, for each word that starts the reading but doesn't cover it; best first. */
        fun firstClauses(): List<List<Node>> = startsAt[0].filter { it.end < n }
            .sortedBy { dict.connection(0, it.lid) + it.cost + it.backward }
            .map { first ->
                val clause = mutableListOf(first)
                while (true) {
                    val next = clause.last().next ?: break
                    if (next === eos || next.end == n || !dict.functional[next.lid]) break
                    clause += next
                }
                clause
            }
    }

    private class Dictionary(input: InputStream) {
        private val data = DataInputStream(GZIPInputStream(input, 1 shl 16))
        private val size = data.readInt()
        private val matrix = shorts(size * size)
        val functional = BooleanArray(size).also { f -> bytes(size).forEachIndexed { i, b -> f[i] = b != 0.toByte() } }
        private val readingCount = data.readInt()
        private val entryCount = data.readInt()
        private val readingCharCount = data.readInt()
        private val surfaceCharCount = data.readInt()
        private val readingChars = chars(readingCharCount)
        private val readingStarts = ints(readingCount + 1)
        private val entryStarts = ints(readingCount + 1)
        private val lids = shorts(entryCount)
        private val rids = shorts(entryCount)
        private val costs = shorts(entryCount)
        private val surfaceStarts = ints(entryCount + 1)
        private val surfaceChars = chars(surfaceCharCount)

        fun connection(rid: Int, lid: Int) = matrix[rid * size + lid].toInt()

        /** Calls [found] for every word whose reading is kana[start, end), end <= limit; surface null = the reading itself. */
        fun lookup(kana: String, start: Int, limit: Int, found: (end: Int, surface: String?, lid: Int, rid: Int, cost: Int) -> Unit) {
            // Readings sharing kana[start, end) form a range of the sorted list; narrow it one character at a time.
            var lo = 0
            var hi = readingCount
            for (end in start + 1..limit) {
                val k = end - start - 1
                val c = kana[end - 1]
                lo = bound(lo, hi, k) { it >= c.code }
                hi = bound(lo, hi, k) { it > c.code }
                if (lo == hi) return
                if (readingStarts[lo + 1] - readingStarts[lo] == k + 1) { // shorter readings sort first
                    for (e in entryStarts[lo] until entryStarts[lo + 1]) {
                        val from = surfaceStarts[e]
                        val to = surfaceStarts[e + 1]
                        found(end, if (from == to) null else String(surfaceChars, from, to - from), lids[e].toInt(), rids[e].toInt(), costs[e].toInt())
                    }
                }
            }
        }

        /** First reading in [lo, hi) whose k-th character (-1 if too short) satisfies [past]. */
        private inline fun bound(lo: Int, hi: Int, k: Int, past: (Int) -> Boolean): Int {
            var a = lo
            var b = hi
            while (a < b) {
                val m = (a + b) ushr 1
                val s = readingStarts[m]
                val ch = if (readingStarts[m + 1] - s > k) readingChars[s + k].code else -1
                if (past(ch)) b = m else a = m + 1
            }
            return a
        }

        private fun bytes(count: Int) = ByteArray(count).also(data::readFully)
        private fun shorts(count: Int) = ShortArray(count).also { ByteBuffer.wrap(bytes(count * 2)).asShortBuffer().get(it) }
        private fun ints(count: Int) = IntArray(count).also { ByteBuffer.wrap(bytes(count * 4)).asIntBuffer().get(it) }
        private fun chars(count: Int) = CharArray(count).also { ByteBuffer.wrap(bytes(count * 2)).asCharBuffer().get(it) }
    }

    private companion object {
        const val UNKNOWN_COST = 10000
        const val UNREACHABLE = 1 shl 28

        fun isKana(c: Char) = c in 'ぁ'..'ゖ' || c == 'ー' || c == 'ゝ' || c == 'ゞ'

        fun katakana(kana: String) = String(CharArray(kana.length) { kana[it].let { c -> if (c in 'ぁ'..'ゖ') c + 0x60 else c } })
    }
}
