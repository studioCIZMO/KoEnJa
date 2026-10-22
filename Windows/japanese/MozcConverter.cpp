#include "MozcConverter.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <fstream>
#include <unordered_set>

#include "../engine/src/Text.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace rea {

static constexpr int UNKNOWN_COST = 10000;
static constexpr int UNREACHABLE = 1 << 28;

static bool isKana(char16_t c) { return (c >= u'ぁ' && c <= u'ゖ') || c == u'ー' || c == u'ゝ' || c == u'ゞ'; }

static std::u16string katakana(std::u16string kana) {
    for (auto& c : kana) if (c >= u'ぁ' && c <= u'ゖ') c += 0x60;
    return kana;
}

/** The dictionary file's sections, read in place (big-endian, as build.py writes them). */
struct MozcConverter::Dictionary {
    const uint8_t* data = nullptr;
    size_t length = 0;
    int size = 0, readingCount = 0, entryCount = 0;
    size_t matrix = 0, functionalAt = 0, readingChars = 0, readingStarts = 0, entryStarts = 0;
    size_t lids = 0, rids = 0, costs = 0, surfaceStarts = 0, surfaceChars = 0;

    /** Maps [path]; null if it can't, or it's too short for its own counts. */
    static std::unique_ptr<Dictionary> map(const std::filesystem::path& path) {
        auto d = std::make_unique<Dictionary>();
#ifdef _WIN32
        // The view keeps the file and the mapping open; the pages are the file's, shared by every process.
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return nullptr;
        LARGE_INTEGER bytes{};
        HANDLE mapping = GetFileSizeEx(file, &bytes) && bytes.QuadPart > 0 ? CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
        CloseHandle(file);
        if (!mapping) return nullptr;
        d->data = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
        CloseHandle(mapping);
        d->length = size_t(bytes.QuadPart);
#else
        int fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) return nullptr;
        struct stat st{};
        void* at = fstat(fd, &st) == 0 && st.st_size > 0 ? mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_SHARED, fd, 0) : MAP_FAILED;
        close(fd);
        if (at != MAP_FAILED) d->data = static_cast<const uint8_t*>(at);
        d->length = size_t(st.st_size);
#endif
        if (!d->data || !d->read()) return nullptr;
        return d;
    }

    ~Dictionary() {
        if (!data) return;
#ifdef _WIN32
        UnmapViewOfFile(data);
#else
        munmap(const_cast<uint8_t*>(data), length);
#endif
    }

    /** The section offsets; false if the file is shorter than they say (a truncated copy). */
    bool read() {
        size_t end = 4;
        auto section = [&](size_t bytes) { size_t at = end; end += bytes; return at; };
        auto fits = [&] { return end <= length; };
        if (!fits()) return false;
        size = int32(0);
        if (size <= 0) return false;
        matrix = section(2 * size_t(size) * size);
        functionalAt = section(size_t(size));
        size_t counts = section(16);
        if (!fits()) return false;
        readingCount = int32(counts);
        entryCount = int32(counts + 4);
        readingChars = section(2 * size_t(uint32_t(int32(counts + 8))));
        readingStarts = section(4 * (size_t(readingCount) + 1));
        entryStarts = section(4 * (size_t(readingCount) + 1));
        lids = section(2 * size_t(entryCount));
        rids = section(2 * size_t(entryCount));
        costs = section(2 * size_t(entryCount));
        surfaceStarts = section(4 * (size_t(entryCount) + 1));
        surfaceChars = section(2 * size_t(uint32_t(int32(counts + 12))));
        return readingCount >= 0 && entryCount >= 0 && fits();
    }

    int32_t int32(size_t at) const { return int32_t(uint32_t(data[at]) << 24 | uint32_t(data[at + 1]) << 16 | uint32_t(data[at + 2]) << 8 | data[at + 3]); }
    int16_t int16(size_t at) const { return int16_t(uint16_t(data[at] << 8 | data[at + 1])); }
    char16_t char16(size_t at) const { return char16_t(data[at] << 8 | data[at + 1]); }

    int readingStart(int i) const { return int32(readingStarts + 4 * size_t(i)); }
    int surfaceStart(int e) const { return int32(surfaceStarts + 4 * size_t(e)); }

    bool functional(int lid) const { return data[functionalAt + lid] != 0; }
    int connection(int rid, int lid) const { return int16(matrix + 2 * (size_t(rid) * size + lid)); }

    /** Calls [found] for every word whose reading is kana[start, end), end <= limit; surface null = the reading itself. */
    template <class F> void lookup(const std::u16string& kana, int start, int limit, F found) const {
        // Readings sharing kana[start, end) form a range of the sorted list; narrow it one character at a time.
        int lo = 0;
        int hi = readingCount;
        for (int end = start + 1; end <= limit; end++) {
            int k = end - start - 1;
            int c = kana[end - 1];
            lo = bound(lo, hi, k, [c](int ch) { return ch >= c; });
            hi = bound(lo, hi, k, [c](int ch) { return ch > c; });
            if (lo == hi) return;
            if (readingStart(lo + 1) - readingStart(lo) == k + 1) { // shorter readings sort first
                for (int e = int32(entryStarts + 4 * size_t(lo)); e < int32(entryStarts + 4 * size_t(lo + 1)); e++) {
                    int from = surfaceStart(e);
                    int to = surfaceStart(e + 1);
                    std::u16string surface;
                    for (int i = from; i < to; i++) surface += char16(surfaceChars + 2 * size_t(i));
                    found(end, from == to ? nullptr : &surface, int(int16(lids + 2 * size_t(e))), int(int16(rids + 2 * size_t(e))),
                          int(int16(costs + 2 * size_t(e))));
                }
            }
        }
    }

    /** First reading in [lo, hi) whose k-th character (-1 if too short) satisfies [past]. */
    template <class P> int bound(int lo, int hi, int k, P past) const {
        int a = lo;
        int b = hi;
        while (a < b) {
            int m = int(unsigned(a + b) >> 1);
            int s = readingStart(m);
            int ch = readingStart(m + 1) - s > k ? int(char16(readingChars + 2 * size_t(s + k))) : -1;
            if (past(ch)) b = m; else a = m + 1;
        }
        return a;
    }
};

struct MozcConverter::Node {
    int start, end;
    std::u16string reading, surface;
    int lid, rid, cost;
    int forward = 0; // best cost from BOS through this node
    int backward = 0; // best cost from after this node to EOS
    Node* next = nullptr; // the node after this one on that best path
};

struct MozcConverter::Lattice {
    const Dictionary& dict;
    const std::unordered_map<std::u16string, int>& learned;
    const std::u16string& kana;
    int n;
    std::deque<Node> nodes; // stable addresses
    Node* bos;
    Node* eos;
    std::vector<std::vector<Node*>> endsAt, startsAt;

    Lattice(const Dictionary& dict, const std::unordered_map<std::u16string, int>& learned, const std::u16string& kana)
        : dict(dict), learned(learned), kana(kana), n(int(kana.size())), endsAt(n + 1), startsAt(n + 1) {
        bos = &nodes.emplace_back(Node{0, 0, u"", u"", 0, 0, 0});
        eos = &nodes.emplace_back(Node{n, n, u"", u"", 0, 0, 0});
        endsAt[0].push_back(bos);
        startsAt[n].push_back(eos);
        int limit = 0; // end of the kana run at i: words don't cross punctuation
        for (int i = 0; i < n; i++) {
            if (limit <= i) {
                limit = i;
                while (limit < n && isKana(kana[limit])) limit++;
            }
            if (!isKana(kana[i])) { // punctuation and the like pass through, as at a sentence boundary
                link(Node{i, i + 1, kana.substr(i, 1), kana.substr(i, 1), 0, 0, 0});
                continue;
            }
            bool found = false;
            dict.lookup(kana, i, limit, [&](int end, const std::u16string* surface, int lid, int rid, int cost) {
                auto reading = kana.substr(i, end - i);
                auto written = surface ? *surface : reading;
                auto count = learned.find(reading + u"\t" + written);
                int boost = count == learned.end() ? 0 : 1000 * std::min(count->second, 4);
                int kanaOnly = surface ? 0 : 1; // ties (よし/良し) go to the written form
                link(Node{i, end, reading, written, lid, rid, cost + kanaOnly - boost});
                found = true;
            });
            if (!found) link(Node{i, i + 1, kana.substr(i, 1), kana.substr(i, 1), 0, 0, UNKNOWN_COST});
        }
        // Forward Viterbi (nodes by start), then backward (by end).
        for (int i = 0; i <= n; i++) for (Node* node : startsAt[i]) {
            // Nothing ends where node starts (no 1-kana word before it): unreachable.
            int best = UNREACHABLE;
            bool any = false;
            for (Node* prev : endsAt[node->start]) {
                int c = prev->forward + dict.connection(prev->rid, node->lid);
                if (!any || c < best) best = c, any = true;
            }
            node->forward = best + node->cost;
        }
        for (int i = n; i >= 0; i--) for (Node* node : endsAt[i]) {
            if (node == eos) continue;
            Node* best = nullptr;
            int bestCost = 0;
            for (Node* next : startsAt[node->end]) {
                int c = dict.connection(node->rid, next->lid) + next->cost + next->backward;
                if (!best || c < bestCost) best = next, bestCost = c;
            }
            node->next = best;
            node->backward = bestCost;
        }
    }

    void link(Node&& node) {
        Node* at = &nodes.emplace_back(std::move(node));
        startsAt[at->start].push_back(at);
        endsAt[at->end].push_back(at);
    }

    /** Whole-reading conversions, best first: A* from EOS back to BOS, the forward costs as the exact heuristic. */
    std::vector<std::vector<Node*>> nBest(size_t count) {
        struct Partial { Node* node; int cost; int after; }; // cost: everything after node; after: index, -1 = none
        std::vector<Partial> partials{{eos, 0, -1}};
        // java.util.PriorityQueue's heap, step for step: equal costs come out in the Kotlin's order.
        std::vector<int> heap{0};
        auto key = [&](int p) { return partials[p].node->forward + partials[p].cost; };
        auto offer = [&](int x) {
            size_t k = heap.size();
            heap.push_back(x);
            while (k > 0) {
                size_t parent = (k - 1) >> 1;
                if (key(x) >= key(heap[parent])) break;
                heap[k] = heap[parent];
                k = parent;
            }
            heap[k] = x;
        };
        auto poll = [&] {
            int result = heap[0];
            int x = heap.back();
            heap.pop_back();
            size_t size = heap.size();
            if (size > 0) {
                size_t k = 0, half = size >> 1;
                while (k < half) {
                    size_t child = 2 * k + 1, right = child + 1;
                    if (right < size && key(heap[child]) > key(heap[right])) child = right;
                    if (key(x) <= key(heap[child])) break;
                    heap[k] = heap[child];
                    k = child;
                }
                heap[k] = x;
            }
            return result;
        };
        std::vector<std::vector<Node*>> out;
        std::unordered_set<std::u16string> seen;
        int pops = 0;
        while (out.size() < count && !heap.empty() && pops++ < 5000) {
            int p = poll();
            if (partials[p].node == bos) {
                std::vector<Node*> path;
                std::u16string text;
                for (int a = partials[p].after; a >= 0; a = partials[a].after) {
                    if (partials[a].node == eos) continue;
                    path.push_back(partials[a].node);
                    text += partials[a].node->surface;
                }
                if (seen.insert(text).second) out.push_back(std::move(path));
                continue;
            }
            Node* node = partials[p].node;
            for (Node* prev : endsAt[node->start]) {
                partials.push_back({prev, partials[p].cost + node->cost + dict.connection(prev->rid, node->lid), p});
                offer(int(partials.size() - 1));
            }
        }
        return out;
    }

    /** The first word plus the particles after it on its best path, for each word that starts the reading but doesn't cover it; best first. */
    std::vector<std::vector<Node*>> firstClauses() {
        std::vector<Node*> firsts;
        for (Node* node : startsAt[0]) if (node->end < n) firsts.push_back(node);
        std::stable_sort(firsts.begin(), firsts.end(), [&](Node* a, Node* b) {
            return dict.connection(0, a->lid) + a->cost + a->backward < dict.connection(0, b->lid) + b->cost + b->backward;
        });
        std::vector<std::vector<Node*>> out;
        for (Node* first : firsts) {
            std::vector<Node*> clause{first};
            while (true) {
                Node* next = clause.back()->next;
                if (!next || next == eos || next->end == n || !dict.functional(next->lid)) break;
                clause.push_back(next);
            }
            out.push_back(std::move(clause));
        }
        return out;
    }
};

MozcConverter::MozcConverter(const std::filesystem::path& dictionary, std::optional<std::filesystem::path> learningFile)
    : dict(Dictionary::map(dictionary)), learningFile(std::move(learningFile)) {
    if (!this->learningFile) return;
    std::ifstream file(*this->learningFile, std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto parts = utf16(line);
        auto first = parts.find(u'\t');
        auto second = first == std::u16string::npos ? first : parts.find(u'\t', first + 1);
        if (second == std::u16string::npos) continue;
        auto third = parts.find(u'\t', second + 1);
        auto count = toInt(parts.substr(second + 1, third == std::u16string::npos ? third : third - second - 1));
        if (count) learned[parts.substr(0, second)] = *count;
    }
}

MozcConverter::~MozcConverter() = default;

std::vector<ConversionCandidate> MozcConverter::candidates(const std::u16string& kana) {
    if (kana.empty()) return {};
    std::unordered_map<std::u16string, std::vector<Word>> paths;
    std::vector<ConversionCandidate> out;
    if (dict) {
        Lattice lattice(*dict, learned, kana);
        auto add = [&](const std::vector<Node*>& path) {
            std::u16string text;
            for (Node* node : path) text += node->surface;
            if (paths.count(text)) return;
            auto& words = paths[text];
            for (Node* node : path) words.push_back({node->reading, node->surface, node->lid});
            out.push_back({text, path.back()->end});
        };
        for (auto& path : lattice.nBest(10)) add(path);
        auto clauses = lattice.firstClauses();
        for (size_t i = 0; i < clauses.size() && i < 10; i++) add(clauses[i]);
    }
    for (auto& plain : {kana, katakana(kana)}) {
        if (!paths.count(plain)) out.push_back({plain, int(kana.size())});
    }
    lastKana = kana;
    lastPaths = std::move(paths);
    return out;
}

void MozcConverter::learn(const ConversionCandidate& candidate, const std::u16string& kana) {
    if (kana != lastKana) candidates(kana);
    auto path = lastPaths.find(candidate.text);
    if (path == lastPaths.end()) return;
    for (auto& word : path->second) {
        if (word.lid == 0 || dict->functional(word.lid)) continue; // passed-through character, particle
        auto key = word.reading + u"\t" + word.surface;
        auto prefix = word.reading + u"\t";
        std::erase_if(learned, [&](auto& e) { return startsWith(e.first, prefix) && e.first != key; }); // the latest choice wins
        learned[key]++;
    }
}

void MozcConverter::save() {
    if (!learningFile) return;
    auto temp = *learningFile;
    temp += ".tmp"; // a crash mid-write keeps the old file
    {
        std::ofstream out(temp, std::ios::binary);
        for (auto& [key, count] : learned) out << utf8(key) << '\t' << count << '\n';
        if (!out) return;
    }
    std::error_code error;
    std::filesystem::rename(temp, *learningFile, error);
}

} // namespace rea
