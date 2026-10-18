// swift-tools-version:6.1
// Types key sequences through SeamtypeCore (macOS) and writes what each added to the text:
// the expected output the Kotlin engine (Android/engine) is tested against.
import PackageDescription

let package = Package(
    name: "golden",
    platforms: [.macOS(.v13)],
    dependencies: [.package(path: "../../../Packages/SeamtypeCore")],
    targets: [.executableTarget(name: "golden", dependencies: ["SeamtypeCore"])],
    swiftLanguageModes: [.v5]
)
