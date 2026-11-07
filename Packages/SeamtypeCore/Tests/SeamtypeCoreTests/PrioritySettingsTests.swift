import Foundation
import Testing
@testable import SeamtypeCore

/// REA: KoEnJa's priority language, its key and automatic updates in settings.json.
@Suite struct PrioritySettingsTests {
    @Test func savedAndReadBack() throws {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("koenja-priority-\(UUID().uuidString)")
        let url = dir.appendingPathComponent("settings.json")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }
        try Data(#"{ "japanese": true, "priorityKey": "nonsense" }"#.utf8).write(to: url)
        var settings = Settings.load(from: url)
        #expect(settings.priority == nil && settings.priorityKey == nil && !settings.autoInstallUpdates)
        settings.priority = "japanese"
        settings.priorityKey = .rightControl
        settings.autoInstallUpdates = true
        try settings.save(to: url)
        let read = Settings.load(from: url)
        #expect(read.priority == "japanese" && read.priorityKey == .rightControl && read.autoInstallUpdates && read.japanese)
        settings.priority = nil
        settings.priorityKey = nil
        try settings.save(to: url)
        let text = try String(contentsOf: url, encoding: .utf8)
        #expect(!text.contains("priority"))
    }
}
