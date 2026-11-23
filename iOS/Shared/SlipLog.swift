import Foundation

/// What the keyboard corrected, one line per event, in the App Group (slips.log), in every build:
/// which keys were taken back and retyped as which (SLIP k→l dx dy toward learned), what the
/// bar replaced (PICK, FIX), what the touch model moved (NEAR, EDGE, LEARN). The app shows it
/// under 오타 기록 with a share button, so a tester can send it without a debug build. No
/// keystrokes as such: only the words a correction touched, never in a password field.
/// Capped: past 256 KB the oldest half goes.
enum SlipLog {
    static let url = FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: "group.com.cizmo.koenja")?
        .appendingPathComponent("slips.log")
    static let cap = 256 * 1024
    private static let stamp: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "MM-dd HH:mm:ss"
        return f
    }()

    static func write(_ line: @autoclosure () -> String) {
        guard let url, let data = "\(stamp.string(from: Date())) \(line())\n".data(using: .utf8) else { return }
        do {
            let handle = try FileHandle(forWritingTo: url)
            defer { try? handle.close() }
            let end = try handle.seekToEnd()
            try handle.write(contentsOf: data)
            // ponytail: trim by rewrite; the app clearing in the same millisecond could bring the tail back
            if end + UInt64(data.count) > cap, let all = try? Data(contentsOf: url) {
                var tail = all.suffix(all.count / 2)
                if let newline = tail.firstIndex(of: UInt8(ascii: "\n")) { tail = tail.suffix(from: newline + 1) }
                try Data(tail).write(to: url, options: .atomic)
            }
        } catch {
            try? data.write(to: url) // no file yet (or unwritable: then nothing is logged)
        }
    }

    static func read() -> String { url.flatMap { try? String(contentsOf: $0, encoding: .utf8) } ?? "" }

    static func clear() { url.map { try? FileManager.default.removeItem(at: $0) } }

    /// "k→l: 12" lines from the SLIP events, most frequent first: which keys get mistyped for which.
    static func summary(_ text: String) -> [(pair: String, count: Int)] {
        var counts: [String: Int] = [:]
        for line in text.split(separator: "\n") {
            let p = line.split(separator: " ")
            if p.count > 3, p[2] == "SLIP" { counts[String(p[3]), default: 0] += 1 }
        }
        return counts.map { (pair: $0.key, count: $0.value) }.sorted { $0.count > $1.count || ($0.count == $1.count && $0.pair < $1.pair) }
    }
}
