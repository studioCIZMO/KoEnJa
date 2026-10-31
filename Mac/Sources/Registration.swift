import AppKit
import Carbon
import SeamtypeCore

/// `Araso --register`, run from the freshly installed copy by every install path (the pkg's
/// postinstall, get.sh, install.sh), after the old process has been stopped.
@MainActor
enum Registration {
    /// Registers this bundle as an input method and enables its input source; returns the status.
    static func run() -> OSStatus {
        forgetOtherCopies()
        let filter = [kTISPropertyInputSourceID as String: ArasoSource.id] as CFDictionary
        func listed() -> TISInputSource? {
            (TISCreateInputSourceList(filter, true)?.takeRetainedValue() as? [TISInputSource])?.first
        }
        // Always registered, even when macOS lists the source already: it may still point at a
        // copy that is about to go (the pkg replacing one in ~/Library), and then macOS drops the
        // source, and the user's choice to use it, when that copy disappears.
        let status = TISRegisterInputSource(Bundle.main.bundleURL as CFURL)
        // Enabling one the user has already added is left out, as Squirrel and vChewing do.
        if status == noErr, let source = listed(),
           TISGetInputSourceProperty(source, kTISPropertyInputSourceIsEnabled)
               .map({ CFBooleanGetValue(Unmanaged<CFBoolean>.fromOpaque($0).takeUnretainedValue()) }) != true {
            TISEnableInputSource(source)
        }
        // Updating from Legato or Seamtype: macOS sees another input method, and the old one goes with its
        // bundle. Someone typing with it gets Araso instead of whatever macOS falls back to.
        if status == noErr, Predecessor.bundleIDs.contains(ArasoSource.currentID ?? ""), let source = listed() {
            TISSelectInputSource(source)
        }
        return status
    }

    /// `Araso --registered`: whether macOS lists the input source, enabled or not. The list is
    /// cached per process, so the pkg asks a fresh one after each `--register`: one run while
    /// LaunchServices was still taking in the replaced bundle reported success and left no source.
    static func isListed() -> Bool {
        let filter = [kTISPropertyInputSourceID as String: ArasoSource.id] as CFDictionary
        return !((TISCreateInputSourceList(filter, true)?.takeRetainedValue() as? [TISInputSource]) ?? []).isEmpty
    }

    /// Any other copy LaunchServices knows (a build folder, a stale download, an old path) can be
    /// the one macOS launches as the input method, and then nothing types until a restart.
    private static func forgetOtherCopies() {
        let ours = Bundle.main.bundleURL.resolvingSymlinksInPath().standardizedFileURL
        let lsregister = "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
        for url in NSWorkspace.shared.urlsForApplications(withBundleIdentifier: ArasoSource.id)
        where url.resolvingSymlinksInPath().standardizedFileURL != ours {
            Uninstaller.launch(lsregister, ["-u", url.path])
        }
        // Legato's and Seamtype's leftovers (a build folder, a download) are another input method to macOS;
        // its installed copies are removed by the install paths once this one is registered.
        for url in Predecessor.bundleIDs.flatMap(NSWorkspace.shared.urlsForApplications(withBundleIdentifier:))
        where url.deletingLastPathComponent().lastPathComponent != "Input Methods" {
            Uninstaller.launch(lsregister, ["-u", url.path])
        }
        Uninstaller.launch(lsregister, ["-f", ours.path])
    }
}
