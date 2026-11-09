import AppKit
import SeamtypeCore

/// Updates: asks GitHub for the latest release's version (nothing else is sent) once a day from
/// the input method, each time the settings window opens, and on Check Now. A newer one shows in
/// the input menu and in About, with what changed in it (the release notes' first section), whose Install button
/// downloads `KoEnJa-Mac.zip` and replaces this copy with it (`install()`). Nothing installs on its own unless
/// "Install updates automatically" is on: then the input method installs what its daily check finds.
///
/// The input method and the settings window are separate processes; what the last check found is
/// shared through the bundle's user defaults.
@MainActor
final class Updater: ObservableObject {
    static let shared = Updater()

    enum State: Equatable {
        case idle, checking, upToDate, downloading, failed(String)
    }

    @Published private(set) var state = State.idle
    /// A release newer than this copy, from the last check by either process.
    @Published private(set) var available: Release?

    private static let gitHub = URL(string: "https://api.github.com/repos/studioCIZMO/KoEnJa/releases/latest")!
    /// GitHub's latest release, or, for trying an update before it is published (drafts aren't
    /// visible without a login), a file in the same format whose `browser_download_url` may be a
    /// file URL: `defaults write com.cizmo.inputmethod.KoEnJa updateFeed file:///…/latest.json`.
    /// The installer still has to pass the signature check.
    private static var latest: URL {
        UserDefaults.standard.string(forKey: "updateFeed").flatMap(URL.init(string:)) ?? gitHub
    }
    /// KoEnJa: the release's Mac download; a release without one (Windows, Android only) is no update.
    private static let asset = "KoEnJa-Mac.zip"
    private static let interval: TimeInterval = 24 * 60 * 60
    private enum Key {
        static let checked = "updateLastCheck", version = "updateVersion", package = "updatePackage", page = "updatePage", notes = "updateNotes"
    }

    static var currentVersion: String {
        Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "0"
    }

    private var timer: Timer?
    /// Set in the input method (its daily checks): there, with the setting on, an update found is
    /// installed without asking. The settings window only offers it.
    private var installsOnItsOwn = false
    /// The input menu's item was chosen: install what the check finds, and show About when there
    /// is nothing to install or it failed (the input method has no window of its own).
    private var fromMenu = false

    private init() { available = Self.stored() }

    /// What the last check found, if it is still newer than this copy.
    private static func stored() -> Release? {
        let defaults = UserDefaults.standard
        guard let version = defaults.string(forKey: Key.version),
              let package = defaults.string(forKey: Key.package).flatMap(URL.init(string:)),
              Release.isNewer(version, than: currentVersion) else { return nil }
        return Release(version: version, package: package,
                       page: defaults.string(forKey: Key.page).flatMap(URL.init(string:)),
                       notes: defaults.stringArray(forKey: Key.notes) ?? [])
    }

    /// Reads again what the last check found, which the other process may have written.
    func refresh() { available = Self.stored() }

    /// The input method: a check a minute after it starts, then wake-ups every 10 minutes that check only
    /// once a day has passed, while the setting is on.
    func startDailyChecks() {
        installsOnItsOwn = true
        timer?.invalidate()
        let tick = { @MainActor in
            guard Runtime.shared.settings.checkForUpdates else { return }
            Updater.shared.installIfQuiet()
            let last = UserDefaults.standard.object(forKey: Key.checked) as? Date ?? .distantPast
            if Date().timeIntervalSince(last) >= Self.interval { Updater.shared.check() }
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 60) { MainActor.assumeIsolated { tick() } }
        timer = Timer.scheduledTimer(withTimeInterval: 10 * 60, repeats: true) { _ in
            MainActor.assumeIsolated { tick() }
        }
    }

    /// With "Install updates automatically" on, in the input method: installs what a check found,
    /// once no key has come for two minutes and nothing is being typed (get.sh restarts KoEnJa).
    /// Tried after each check and on every timer tick.
    private func installIfQuiet() {
        guard installsOnItsOwn, available != nil, state == .idle || state == .upToDate, Self.replaceable,
              Runtime.shared.settings.autoInstallUpdates, Runtime.shared.settings.checkForUpdates,
              Date().timeIntervalSince(Runtime.shared.lastKey) > 120,
              Runtime.shared.active?.composing != true else { return }
        install()
    }

    /// The input menu's item: installs a newer release, checking first; About shows the outcome
    /// unless it installed (get.sh then restarts KoEnJa).
    func updateFromMenu() {
        fromMenu = true
        // A check or download already running finishes the same way (finishCheck, finishInstall).
        if available != nil { install() } else { check() }
    }

    private func menuDone() {
        guard fromMenu else { return }
        fromMenu = false
        SettingsLauncher.open(tab: .about)
    }

    /// Asks GitHub now.
    func check() {
        guard state != .checking, state != .downloading else { return }
        state = .checking
        var request = URLRequest(url: Self.latest, timeoutInterval: 20)
        request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        request.setValue("KoEnJa/\(Self.currentVersion)", forHTTPHeaderField: "User-Agent")
        URLSession.shared.dataTask(with: request) { data, response, error in
            // A file (the updateFeed override) has no HTTP status.
            let status = (response as? HTTPURLResponse)?.statusCode ?? 200
            let release = data.flatMap { Release(gitHubJSON: $0, asset: Self.asset) }
            let noMac = data.map { Release.lacks(Self.asset, gitHubJSON: $0) } ?? false
            DispatchQueue.main.async {
                MainActor.assumeIsolated {
                    Updater.shared.finishCheck(release, failed: error != nil || status != 200, noMac: noMac)
                }
            }
        }.resume()
    }

    private func finishCheck(_ release: Release?, failed: Bool, noMac: Bool) {
        guard state == .checking else { return } // an install started meanwhile owns the state
        if !failed, release == nil, noMac {
            UserDefaults.standard.set(Date(), forKey: Key.checked)
            state = .upToDate
            menuDone()
            return
        }
        guard !failed, let release else {
            state = .failed(L("Couldn't reach GitHub. Try again later."))
            menuDone()
            return
        }
        let defaults = UserDefaults.standard
        defaults.set(Date(), forKey: Key.checked)
        defaults.set(release.version, forKey: Key.version)
        defaults.set(release.package.absoluteString, forKey: Key.package)
        defaults.set(release.page?.absoluteString, forKey: Key.page)
        defaults.set(release.notes, forKey: Key.notes)
        available = Release.isNewer(release.version, than: Self.currentVersion) ? release : nil
        state = available == nil ? .upToDate : .idle
        if fromMenu, available != nil {
            install()
        } else {
            menuDone()
            installIfQuiet()
        }
    }

    /// The copy get.sh installs, which this updater can replace (the user owns the folder).
    nonisolated static var replaceable: Bool {
        let bundle = Bundle.main.bundleURL.resolvingSymlinksInPath()
        let folder = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Input Methods")
        return bundle.deletingLastPathComponent().standardizedFileURL == folder.resolvingSymlinksInPath().standardizedFileURL
            && FileManager.default.isWritableFile(atPath: folder.path)
    }

    /// KoEnJa: downloads `KoEnJa-Mac.zip` and installs it with the bundled get.sh, which checks
    /// that the app is CIZMO's and notarized before it replaces this copy, then stops the old one
    /// (macOS starts the new one on the next key) and registers the new one. No password: the copy
    /// is in ~/Library/Input Methods. A copy elsewhere gets the release page instead.
    func install() {
        guard let release = available, state != .downloading, state != .checking else { return }
        guard Self.replaceable, let script = Bundle.main.url(forResource: "get", withExtension: "sh") else {
            NSWorkspace.shared.open(release.page ?? release.package)
            fromMenu = false // the release page is the outcome
            return
        }
        state = .downloading
        URLSession.shared.downloadTask(with: release.package) { file, response, error in
            let status = (response as? HTTPURLResponse)?.statusCode ?? 200
            var failure: InstallFailure? = .download
            if let file, error == nil, status == 200 { failure = Self.run(script, zip: file) }
            DispatchQueue.main.async {
                MainActor.assumeIsolated { Updater.shared.finishInstall(failure) }
            }
        }.resume()
    }

    enum InstallFailure { case download, unsigned, other }

    /// Runs get.sh on the downloaded zip; nil when it installed. Its output goes to a file, not a
    /// pipe: get.sh stops this process, and a pipe with no reader would stop get.sh with it. A
    /// successful install stops this process before it can clean up, so each run first removes
    /// what earlier ones left.
    nonisolated private static func run(_ script: URL, zip: URL) -> InstallFailure? {
        let temp = FileManager.default.temporaryDirectory
        for old in (try? FileManager.default.contentsOfDirectory(at: temp, includingPropertiesForKeys: nil)) ?? []
        where old.lastPathComponent.hasPrefix("koenja-update-") { try? FileManager.default.removeItem(at: old) }
        let work = temp.appendingPathComponent("koenja-update-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: work) } // reached only when get.sh failed
        try? FileManager.default.createDirectory(at: work, withIntermediateDirectories: true)
        let saved = work.appendingPathComponent("KoEnJa.zip"), log = work.appendingPathComponent("get.log")
        guard (try? FileManager.default.moveItem(at: zip, to: saved)) != nil,
              FileManager.default.createFile(atPath: log.path, contents: nil),
              let output = try? FileHandle(forWritingTo: log) else { return .download }
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/sh")
        process.arguments = [script.path]
        process.environment = ["KOENJA_ZIP": saved.path, "HOME": NSHomeDirectory(), "PATH": "/usr/bin:/bin:/usr/sbin:/sbin"]
        process.standardOutput = output
        process.standardError = output
        guard (try? process.run()) != nil else { return .other }
        process.waitUntilExit()
        let said = (try? String(contentsOf: log, encoding: .utf8)) ?? ""
        return process.terminationStatus == 0 ? nil : said.contains("isn't signed") ? .unsigned : .other
    }

    private func finishInstall(_ failure: InstallFailure?) {
        switch failure {
        case nil: state = .idle
        case .download: state = .failed(L("Couldn't download the update. Try again later."))
        case .unsigned: state = .failed(L("The download isn't KoEnJa signed by CIZMO and notarized, so nothing was installed."))
        case .other: state = .failed(L("Couldn't install the update."))
        }
        menuDone() // a successful install stops this process first
    }
}
