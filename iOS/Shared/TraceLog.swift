import Foundation

/// Debug builds only: what the keyboard did, one line per event, in its own Library/trace.log
/// (the App Group needs Full Access), to compare with what the app showed. Pull it with
/// `xcrun devicectl device copy from --domain-type appDataContainer --domain-identifier com.netmaru.rea.keyboard`.
enum TraceLog {
    #if DEBUG
    private static let url: URL? = FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask).first?
        .appendingPathComponent("trace.log")
    #endif

    /// The process's memory as iOS counts it against the extension's limit (MB).
    static var footprint: String {
        #if DEBUG
        var info = task_vm_info_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<task_vm_info_data_t>.size / MemoryLayout<natural_t>.size)
        let kr = withUnsafeMutablePointer(to: &info) {
            $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) { task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count) }
        }
        return kr == KERN_SUCCESS ? "\(info.phys_footprint / 1_048_576)MB" : "?"
        #else
        return ""
        #endif
    }

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
