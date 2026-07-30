import AppKit

@main
struct GenerateIcon {
    static func main() throws {
        guard CommandLine.arguments.count == 2 else {
            throw IconError.missingOutputPath
        }

        let size = 1024
        guard let bitmap = NSBitmapImageRep(
            bitmapDataPlanes: nil,
            pixelsWide: size,
            pixelsHigh: size,
            bitsPerSample: 8,
            samplesPerPixel: 4,
            hasAlpha: true,
            isPlanar: false,
            colorSpaceName: .deviceRGB,
            bytesPerRow: 0,
            bitsPerPixel: 0
        ), let context = NSGraphicsContext(bitmapImageRep: bitmap) else {
            throw IconError.couldNotCreateBitmap
        }

        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = context

        let tile = NSBezierPath(roundedRect: NSRect(x: 32, y: 32, width: 960, height: 960), xRadius: 220, yRadius: 220)
        let gradient = NSGradient(
            starting: NSColor(red: 0.20, green: 0.65, blue: 1.00, alpha: 1),
            ending: NSColor(red: 0.09, green: 0.36, blue: 0.86, alpha: 1)
        )
        gradient?.draw(in: tile, angle: -45)

        NSColor.white.setStroke()

        let arrow = NSBezierPath()
        arrow.lineWidth = 78
        arrow.lineCapStyle = .round
        arrow.lineJoinStyle = .round
        arrow.move(to: NSPoint(x: 512, y: 790))
        arrow.line(to: NSPoint(x: 512, y: 380))
        arrow.move(to: NSPoint(x: 352, y: 524))
        arrow.line(to: NSPoint(x: 512, y: 364))
        arrow.line(to: NSPoint(x: 672, y: 524))
        arrow.stroke()

        let baseline = NSBezierPath()
        baseline.lineWidth = 78
        baseline.lineCapStyle = .round
        baseline.move(to: NSPoint(x: 290, y: 254))
        baseline.line(to: NSPoint(x: 734, y: 254))
        baseline.stroke()

        context.flushGraphics()
        NSGraphicsContext.restoreGraphicsState()

        guard let png = bitmap.representation(using: .png, properties: [:]) else {
            throw IconError.couldNotEncodePNG
        }
        try png.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
    }

    enum IconError: Error {
        case missingOutputPath
        case couldNotCreateBitmap
        case couldNotEncodePNG
    }
}
