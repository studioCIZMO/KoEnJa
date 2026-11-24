import AppKit
import SeamtypeCore

/// Updates: asks GitHub for the latest release's version (nothing else is sent) once a day from
/// the input method, each time the settings window opens, and on Check Now. A newer one shows in
/// the input menu and in About, with what changed in it (the release notes' first section), whose Install button
/// downloads `KoEnJa-Mac.zip` and replaces this copy with it (`install()`). Nothing installs on its own unless
/// "Install updates automatically" is on: then the input method installs what its daily check finds.
/// A copy this process can't replace (root's, from the installer package run by another account,
/// or one macOS's App Management keeps it from) gets `KoEnJa.pkg` instead, as Araso does: downloaded,
/// checked for CIZMO's Developer ID Installer signature and notarization, and opened in Installer,
/// where macOS asks for the password.
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
    /// The same release's installer package (`KoEnJa.pkg`), for a copy get.sh can't replace.
    @Published private(set) var installer: URL?

    private static let gitHub = URL(string: "https://api.github.com/repos/studioCIZMO/KoEnJa/releases/latest")!
    /// GitHub's latest release, or, for trying an update before it is published (drafts aren't
    /// visible without a login), a file in the same format whose `browser_download_url` may be a
    /// file URL: `defaults write com.cizmo.inputmethod.KoEnJa updateFeed file:///…/latest.json`.
    /// The installer still has to pass the signature check.
    private static var latest: URL {
        UserDefaults.standard.string(forKey: "updateFeed").flatMap(URL.init(string:)) ?? gitHub
    }
    /// KoEnJa: the release's Mac downloads; a release with neither (Windows, Android only) is no update.
    private static let zip = "KoEnJa-Mac.zip", pkg = "KoEnJa.pkg"
    nonisolated private static let teamID = "N723QM4R64"
    private static let interval: TimeInterval = 24 * 60 * 60
    private enum Key {
        static let checked = "updateLastCheck", version = "updateVersion", package = "updatePackage", page = "updatePage", notes = "updateNotes"
        static let installer = "updateInstaller"
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

    private init() { (available, installer) = Self.stored() }

    /// What the last check found, if it is still newer than this copy.
    private static func stored() -> (Release?, URL?) {
        let defaults = UserDefaults.standard
        guard let version = defaults.string(forKey: Key.version),
              let package = defaults.string(forKey: Key.package).flatMap(URL.init(string:)),
              Release.isNewer(version, than: currentVersion) else { return (nil, nil) }
        return (Release(version: version, package: package,
                        page: defaults.string(forKey: Key.page).flatMap(URL.init(string:)),
                        notes: defaults.stringArray(forKey: Key.notes) ?? []),
                defaults.string(forKey: Key.installer).flatMap(URL.init(string:)))
    }

    /// Reads again what the last check found, which the other process may have written.
    func refresh() { (available, installer) = Self.stored() }

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
            // The zip is what get.sh installs; a release with only the package is still an update.
            let installer = data.flatMap { Release(gitHubJSON: $0, asset: Self.pkg) }
            let release = data.flatMap { Release(gitHubJSON: $0, asset: Self.zip) } ?? installer
            let noMac = data.map { Release.lacks(Self.zip, gitHubJSON: $0) && Release.lacks(Self.pkg, gitHubJSON: $0) } ?? false
            DispatchQueue.main.async {
                MainActor.assumeIsolated {
                    Updater.shared.finishCheck(release, installer: installer?.package, failed: error != nil || status != 200, noMac: noMac)
                }
            }
        }.resume()
    }

    private func finishCheck(_ release: Release?, installer: URL?, failed: Bool, noMac: Bool) {
        guard state == .checking else { return } // an install started meanwhile owns the state
        let defaults = UserDefaults.standard
        if !failed, release == nil, noMac {
            // The check counts, so it isn't made again every hour; nothing kept from an earlier one.
            defaults.set(Date(), forKey: Key.checked)
            for key in [Key.version, Key.package, Key.page, Key.notes, Key.installer] { defaults.removeObject(forKey: key) }
            available = nil
            self.installer = nil
            state = .upToDate
            menuDone()
            return
        }
        guard !failed, let release else {
            state = .failed(L("Couldn't reach GitHub. Try again later."))
            menuDone()
            return
        }
        defaults.set(Date(), forKey: Key.checked)
        defaults.set(release.version, forKey: Key.version)
        defaults.set(release.package.absoluteString, forKey: Key.package)
        defaults.set(release.page?.absoluteString, forKey: Key.page)
        defaults.set(release.notes, forKey: Key.notes)
        defaults.set(installer?.absoluteString, forKey: Key.installer)
        available = Release.isNewer(release.version, than: Self.currentVersion) ? release : nil
        self.installer = available == nil ? nil : installer
        state = available == nil ? .upToDate : .idle
        if fromMenu, available != nil {
            install()
        } else {
            menuDone()
            installIfQuiet()
        }
    }

    /// The copy get.sh installs, which this updater can replace: the bundle in /Library/Input
    /// Methods, owned by the user (get.sh makes it so), whose Contents get.sh swaps without a
    /// password. A copy elsewhere (earlier versions' ~/Library, a build folder) would need the
    /// password get.sh asks in a terminal to move there, so it gets the release page instead.
    nonisolated static var replaceable: Bool {
        let bundle = Bundle.main.bundleURL.resolvingSymlinksInPath().standardizedFileURL
        return bundle.path == "/Library/Input Methods/KoEnJa.app" && FileManager.default.isWritableFile(atPath: bundle.path)
    }

    /// KoEnJa: downloads `KoEnJa-Mac.zip` and installs it with the bundled get.sh, which checks
    /// that the app is CIZMO's and notarized before it replaces this copy's Contents, then stops the
    /// old one (macOS starts the new one on the next key) and registers the new one. No password:
    /// the user owns the bundle. A copy that isn't theirs gets the installer package
    /// (`installPackage`), or the release page when the release has none.
    func install() {
        guard let release = available, state != .downloading, state != .checking else { return }
        guard Self.replaceable, release.package.lastPathComponent == Self.zip,
              let script = Bundle.main.url(forResource: "get", withExtension: "sh") else {
            if let installer { installPackage(installer, version: release.version) } else {
                NSWorkspace.shared.open(release.page ?? release.package)
                fromMenu = false // the release page is the outcome
            }
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

    enum InstallFailure { case download, unsigned, locked, other }

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
        if process.terminationStatus == 0 { return nil }
        if said.contains("isn't signed") { return .unsigned }
        return said.contains("can't be changed from here") ? .locked : .other
    }

    private func finishInstall(_ failure: InstallFailure?) {
        switch failure {
        case nil: state = .idle
        case .download: state = .failed(L("Couldn't download the update. Try again later."))
        case .unsigned: state = .failed(L("The download isn't KoEnJa signed by CIZMO and notarized, so nothing was installed."))
        case .locked:
            // macOS's App Management kept get.sh from the bundle: the installer package, with
            // the password prompt, is the way in.
            if let installer, let release = available {
                state = .idle
                installPackage(installer, version: release.version)
                return
            }
            state = .failed(L("Couldn't install the update."))
        case .other: state = .failed(L("Couldn't install the update."))
        }
        menuDone() // a successful install stops this process first
    }

    /// Araso's way: downloads the installer package into ~/Downloads, checks that it is signed
    /// with CIZMO's Developer ID Installer certificate and notarized, and opens it in Installer.
    private func installPackage(_ package: URL, version: String) {
        state = .downloading
        URLSession.shared.downloadTask(with: package) { file, response, error in
            let status = (response as? HTTPURLResponse)?.statusCode ?? 200
            var saved: URL?
            if let file, error == nil, status == 200 {
                let downloads = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
                    ?? FileManager.default.temporaryDirectory
                let target = downloads.appendingPathComponent("KoEnJa-\(version).pkg")
                try? FileManager.default.removeItem(at: target)
                if (try? FileManager.default.moveItem(at: file, to: target)) != nil { saved = target }
            }
            // Checked here, off the main thread: pkgutil may ask Apple about notarization.
            let signed = saved.map(Self.isSignedByCIZMO) ?? false
            DispatchQueue.main.async {
                MainActor.assumeIsolated { Updater.shared.finishPackage(saved, signed: signed) }
            }
        }.resume()
    }

    private func finishPackage(_ package: URL?, signed: Bool) {
        guard let package else {
            state = .failed(L("Couldn't download the update. Try again later."))
            menuDone()
            return
        }
        guard signed else {
            try? FileManager.default.removeItem(at: package)
            state = .failed(L("The downloaded installer isn't signed by CIZMO and notarized, so it was deleted."))
            menuDone()
            return
        }
        state = .idle
        fromMenu = false // Installer is the outcome
        NSWorkspace.shared.open(package)
    }

    /// `pkgutil --check-signature`: a Developer ID Installer certificate with CIZMO's team,
    /// notarized by Apple.
    nonisolated private static func isSignedByCIZMO(_ package: URL) -> Bool {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/sbin/pkgutil")
        process.arguments = ["--check-signature", package.path]
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = FileHandle.nullDevice
        guard (try? process.run()) != nil else { return false }
        let output = String(decoding: pipe.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
        process.waitUntilExit()
        return process.terminationStatus == 0 && Release.isSigned(pkgutilOutput: output, team: teamID)
    }
}
