// Draws KoEnJa's app icon (Finder, Dock, the settings window): 한영일 over 韓英日 on a light plate,
// as KoEnJaMark in SettingsWindow.swift. usage: swift scripts/make-app-icon.swift Resources/AppIcon.icns
import AppKit

guard CommandLine.arguments.count == 2 else { print("usage: swift scripts/make-app-icon.swift <out.icns>"); exit(1) }
let out = CommandLine.arguments[1]
let set = FileManager.default.temporaryDirectory.appendingPathComponent("AppIcon.iconset")
try? FileManager.default.removeItem(at: set)
try! FileManager.default.createDirectory(at: set, withIntermediateDirectories: true)

func png(_ px: Int) -> Data {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px, bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                               bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    let s = CGFloat(px) / 1024, side = 824 * s, o = (CGFloat(px) - side) / 2
    let plate = NSBezierPath(roundedRect: NSRect(x: o, y: o + 10 * s, width: side, height: side),
                             xRadius: side * 0.24, yRadius: side * 0.24)
    let shadow = NSShadow()
    shadow.shadowColor = NSColor.black.withAlphaComponent(0.3); shadow.shadowBlurRadius = 24 * s
    shadow.shadowOffset = NSSize(width: 0, height: -8 * s)
    NSGraphicsContext.saveGraphicsState(); shadow.set()
    NSColor(red: 0.85, green: 0.85, blue: 0.84, alpha: 1).setFill(); plate.fill()
    NSGraphicsContext.restoreGraphicsState()
    let attrs: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: side * 0.25, weight: .heavy),
                                                .foregroundColor: NSColor(red: 0.07, green: 0.07, blue: 0.08, alpha: 1)]
    let lines = ["한영일", "韓英日"].map { NSAttributedString(string: $0, attributes: attrs) }
    let gap = side * 0.04, total = lines.reduce(-gap) { $0 + $1.size().height + gap }
    var y = o + 10 * s + (side + total) / 2
    for l in lines {
        y -= l.size().height
        l.draw(at: NSPoint(x: o + (side - l.size().width) / 2, y: y))
        y -= gap
    }
    NSGraphicsContext.restoreGraphicsState()
    return rep.representation(using: .png, properties: [:])!
}

for p in [16, 32, 128, 256, 512] {
    try! png(p).write(to: set.appendingPathComponent("icon_\(p)x\(p).png"))
    try! png(p * 2).write(to: set.appendingPathComponent("icon_\(p)x\(p)@2x.png"))
}
let t = Process()
t.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
t.arguments = ["-c", "icns", set.path, "-o", out]
try! t.run(); t.waitUntilExit()
exit(t.terminationStatus)
