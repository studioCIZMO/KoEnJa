import AppKit
import SeamtypeCore

/// Updates: asks GitHub for the latest release's version (nothing else is sent) once a day from
/// the input method, each time the settings window opens, and on Check Now. A newer one shows in
/// the input menu and in About, with what changed in it (the release notes' first section), whose Install button opens
/// the release page (KoEnJa: `KoEnJa-Mac.zip` there, no signed installer package). Nothing installs on its own.
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

    /// The input method: a check a minute after it starts, then hourly wake-ups that check only
    /// once a day has passed, while the setting is on.
    func startDailyChecks() {
        timer?.invalidate()
        let tick = { @MainActor in
            guard Runtime.shared.settings.checkForUpdates else { return }
            let last = UserDefaults.standard.object(forKey: Key.checked) as? Date ?? .distantPast
            if Date().timeIntervalSince(last) >= Self.interval { Updater.shared.check() }
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 60) { MainActor.assumeIsolated { tick() } }
        timer = Timer.scheduledTimer(withTimeInterval: 60 * 60, repeats: true) { _ in
            MainActor.assumeIsolated { tick() }
        }
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
        if !failed, release == nil, noMac {
            UserDefaults.standard.set(Date(), forKey: Key.checked)
            state = .upToDate
            return
        }
        guard !failed, let release else {
            state = .failed(L("Couldn't reach GitHub. Try again later."))
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
    }

    /// KoEnJa: opens the release page, where the zip and how to install it are.
    func install() {
        guard let release = available else { return }
        NSWorkspace.shared.open(release.page ?? release.package)
    }
}
