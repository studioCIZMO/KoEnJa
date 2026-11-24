// The menu-bar input source icon from a picture (Mac/icon/menu.jpeg): the lettering tile, cropped
// to its rounded square, drawn in colour (TISIconIsTemplate is off) at 1x and 2x in one TIFF.
// usage: swift scripts/make-menu-icon.swift <in.jpeg> <out.tiff> [--preview <out.png> <pixels>]
import AppKit

let args = CommandLine.arguments
func fail(_ message: String) -> Never { FileHandle.standardError.write(Data((message + "\n").utf8)); exit(1) }
guard args.count >= 3 else { fail("usage: swift make-menu-icon.swift <in.jpeg> <out.tiff> [--preview <out.png> <pixels>]") }
guard let input = NSImage(contentsOf: URL(fileURLWithPath: args[1])) else { fail("\(args[1]) isn't an image") }
let out = URL(fileURLWithPath: args[2])
let S: CGFloat = 16 // points; the input menu draws icons 16 pt high

/// The tile's bounds: the light border around the picture's dark square, found as the bounding
/// box of pixels brighter than the background.
func tileBounds(_ image: NSImage) -> CGRect {
    let cg = image.cgImage(forProposedRect: nil, context: nil, hints: nil)!
    let w = cg.width, h = cg.height
    var data = [UInt8](repeating: 0, count: w * h * 4)
    let ctx = CGContext(data: &data, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                        space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    ctx.draw(cg, in: CGRect(x: 0, y: 0, width: w, height: h))
    var minX = w, minY = h, maxX = 0, maxY = 0
    for y in 0..<h {
        for x in 0..<w where data[(y * w + x) * 4] > 110 { // red channel: the border and letters are near white
            minX = min(minX, x); maxX = max(maxX, x); minY = min(minY, y); maxY = max(maxY, y)
        }
    }
    guard maxX >= minX, maxY >= minY else { fail("no bright pixels: nothing to crop to") }
    return CGRect(x: minX, y: minY, width: maxX - minX + 1, height: maxY - minY + 1)
}

let bounds = tileBounds(input)
guard let cg = input.cgImage(forProposedRect: nil, context: nil, hints: nil)?.cropping(to: bounds) else { fail("couldn't crop") }

func representation(scale: CGFloat) -> NSBitmapImageRep {
    let px = Int(S * scale)
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px, bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                               bytesPerRow: 0, bitsPerPixel: 0)!
    rep.size = NSSize(width: S, height: S)
    NSGraphicsContext.saveGraphicsState()
    let context = NSGraphicsContext(bitmapImageRep: rep)!
    NSGraphicsContext.current = context
    let c = context.cgContext
    c.interpolationQuality = .high
    let rect = CGRect(x: 0, y: 0, width: S, height: S)
    c.addPath(CGPath(roundedRect: rect, cornerWidth: S * 0.22, cornerHeight: S * 0.22, transform: nil))
    c.clip() // the corners outside the tile's own rounding stay transparent
    c.draw(cg, in: rect)
    NSGraphicsContext.restoreGraphicsState()
    return rep
}

let image = NSImage(size: NSSize(width: S, height: S))
image.addRepresentation(representation(scale: 1))
image.addRepresentation(representation(scale: 2))
try! image.tiffRepresentation!.write(to: out)

if let i = args.firstIndex(of: "--preview"), args.count > i + 2 {
    // A magnified look at the 2x rendering, for checking by eye.
    guard let n = Int(args[i + 2]), (16...4096).contains(n) else { fail("--preview size must be 16…4096 pixels") }
    let pixels = CGFloat(n)
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(pixels), pixelsHigh: Int(pixels), bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                               bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    let context = NSGraphicsContext(bitmapImageRep: rep)!
    NSGraphicsContext.current = context
    context.cgContext.interpolationQuality = .none
    let small = representation(scale: 2).cgImage!
    context.cgContext.draw(small, in: CGRect(x: 0, y: 0, width: pixels, height: pixels))
    NSGraphicsContext.restoreGraphicsState()
    try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: args[i + 1]))
}
print("wrote \(out.path) from tile \(Int(bounds.width))x\(Int(bounds.height)) at (\(Int(bounds.minX)),\(Int(bounds.minY)))")
