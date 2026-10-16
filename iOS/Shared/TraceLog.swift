import Foundation

/// Debug builds only: what the keyboard did, one line per event, in its own Library/trace.log
/// (the App Group needs Full Access), to compare with what the app showed. Pull it with
/// `xcrun devicectl device copy from --domain-type appDataContainer --domain-identifier com.netmaru.rea.keyboard`.
enum TraceLog {
    #if DEBUG
    private static let url: URL? = FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask).first?
        .appendingPathComponent("trace.log")
    #endif

    static func write(_ line: @autoclosure () -> String) {
        #if DEBUG
        guard let url, let data = "\(Date().timeIntervalSince1970) \(line())\n".data(using: .utf8) else { return }
        if let handle = try? FileHandle(forWritingTo: url) {
            handle.seekToEndOfFile()
            handle.write(data)
            try? handle.close()
        } else {
            try? data.write(to: url)
        }
        #endif
    }
}
