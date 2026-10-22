import Foundation
import Testing
@testable import SeamtypeCore

/// Updating from Legato (0.2.0) or Seamtype (before it): the settings folder becomes Araso's.
@Suite struct PredecessorTests {
    let base = FileManager.default.temporaryDirectory.appendingPathComponent("araso-predecessor-\(UUID().uuidString)")
    var old: URL { base.appendingPathComponent("Seamtype") }
    var legato: URL { base.appendingPathComponent("Legato") }
    var new: URL { base.appendingPathComponent("Araso") }

    func write(_ text: String, to url: URL) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try text.write(to: url, atomically: true, encoding: .utf8)
    }

    func read(_ url: URL) -> String? { try? String(contentsOf: url, encoding: .utf8) }

    @Test func movesSeamtypesFolder() throws {
        defer { try? FileManager.default.removeItem(at: base) }
        try write("{\"japanese\":true}", to: old.appendingPathComponent("settings.json"))
        try write("memory", to: old.appendingPathComponent("azooKey/memory.txt"))
        SupportDirectory.movePredecessor(in: base)
        #expect(read(new.appendingPathComponent("settings.json")) == "{\"japanese\":true}")
        #expect(read(new.appendingPathComponent("azooKey/memory.txt")) == "memory")
        #expect(!FileManager.default.fileExists(atPath: old.path))
    }

    @Test func keepsArasosFolderWhenBothExist() throws {
        defer { try? FileManager.default.removeItem(at: base) }
        try write("old", to: old.appendingPathComponent("settings.json"))
        try write("new", to: new.appendingPathComponent("settings.json"))
        SupportDirectory.movePredecessor(in: base)
        #expect(read(new.appendingPathComponent("settings.json")) == "new")
        #expect(read(old.appendingPathComponent("settings.json")) == "old")
    }

    @Test func nothingToMoveOnAFreshInstall() {
        defer { try? FileManager.default.removeItem(at: base) }
        SupportDirectory.movePredecessor(in: base)
        #expect(!FileManager.default.fileExists(atPath: new.path))
    }

    @Test func movesLegatosFolder() throws {
        defer { try? FileManager.default.removeItem(at: base) }
        try write("legato", to: legato.appendingPathComponent("settings.json"))
        SupportDirectory.movePredecessor(in: base)
        #expect(read(new.appendingPathComponent("settings.json")) == "legato")
        #expect(!FileManager.default.fileExists(atPath: legato.path))
    }

    /// Someone who updated Seamtype to Legato and kept a stray Seamtype folder: Legato's is the newer.
    @Test func prefersLegatoOverSeamtype() throws {
        defer { try? FileManager.default.removeItem(at: base) }
        try write("seamtype", to: old.appendingPathComponent("settings.json"))
        try write("legato", to: legato.appendingPathComponent("settings.json"))
        SupportDirectory.movePredecessor(in: base)
        #expect(read(new.appendingPathComponent("settings.json")) == "legato")
        #expect(read(old.appendingPathComponent("settings.json")) == "seamtype")
    }
}
