// swift-tools-version:6.1
// Vendored from https://github.com/nyttastudio/araso's ArasoCore (MIT, see LICENSE and UPSTREAM), kept as SeamtypeCore.
// Local changes: iOS platform added, Data excluded from the target (shipped as app resources).
import PackageDescription

let package = Package(
    name: "SeamtypeCore",
    platforms: [.macOS(.v13), .iOS(.v16)],
    products: [.library(name: "SeamtypeCore", targets: ["SeamtypeCore"])],
    dependencies: [
        .package(url: "https://github.com/azooKey/AzooKeyKanaKanjiConverter", .upToNextMinor(from: "0.8.0")),
    ],
    targets: [
        .target(name: "SeamtypeCore", dependencies: [
            .product(name: "KanaKanjiConverterModule", package: "AzooKeyKanaKanjiConverter"),
        ], exclude: ["Data"]),
        .testTarget(name: "SeamtypeCoreTests", dependencies: ["SeamtypeCore"]),
    ],
    swiftLanguageModes: [.v5]
)
