import XCTest

/// The app's setup steps on the system's own screens, for its guide: the app's page in Settings,
/// its Keyboards page, the Full Access notice. Run on a fresh simulator in each language with
/// `TEST_RUNNER_SHOTS=<folder> xcodebuild test -only-testing:REAUITests/SetupShots`.
final class SetupShots: XCTestCase {
    func testSetupScreens() throws {
        guard let folder = ProcessInfo.processInfo.environment["SHOTS"] else { throw XCTSkip("set TEST_RUNNER_SHOTS to take them") }
        func shot(_ name: String) {
            sleep(3) // the screen settles (switch animations, the alert sliding in)
            try? XCUIScreen.main.screenshot().pngRepresentation.write(to: URL(fileURLWithPath: "\(folder)/\(name).png"))
        }
        let app = XCUIApplication()
        app.launch()
        let preset = app.buttons["한국어 + 영어"] // the first-run sheet
        if preset.waitForExistence(timeout: 3) { preset.tap() }
        let open = app.buttons["openSettings"]
        while !open.isHittable { app.swipeUp() }
        open.tap()

        let settings = XCUIApplication(bundleIdentifier: "com.apple.Preferences")
        XCTAssertTrue(settings.wait(for: .runningForeground, timeout: 10))
        func cell(_ labels: [String]) -> XCUIElement {
            settings.staticTexts.matching(NSPredicate(format: "label IN %@", labels)).firstMatch
        }
        let keyboards = cell(["키보드", "キーボード", "Keyboards"])
        if !keyboards.waitForExistence(timeout: 5) {
            // Settings came up at its root (a fresh install): Apps › the app.
            let apps = cell(["앱", "アプリ", "Apps"])
            for _ in 0..<8 where !apps.isHittable { settings.swipeUp() }
            apps.tap()
            let us = cell(["한영일", "KoEnJa", "韓英日"])
            for _ in 0..<8 where !us.waitForExistence(timeout: 2) || !us.isHittable { settings.swipeUp() }
            us.tap()
        }
        XCTAssertTrue(keyboards.waitForExistence(timeout: 10))
        shot("1-app")
        keyboards.tap()
        let switches = settings.switches
        XCTAssertTrue(switches.firstMatch.waitForExistence(timeout: 5))
        switches.element(boundBy: 0).tap()
        let fullAccess = switches.element(boundBy: 1)
        XCTAssertTrue(fullAccess.waitForExistence(timeout: 5)) // shows once the keyboard is on
        shot("2-keyboards")
        fullAccess.tap()
        XCTAssertTrue(settings.alerts.firstMatch.waitForExistence(timeout: 5))
        shot("3-full-access")
        settings.alerts.firstMatch.buttons.matching(NSPredicate(format: "label IN %@", ["허용", "許可", "Allow"])).firstMatch.tap()
        shot("4-done")
    }
}
