import Foundation

@main
struct GenerateICNS {
    static func main() throws {
        guard CommandLine.arguments.count == 3 else {
            throw ICNSError.invalidArguments
        }

        let iconset = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        let output = URL(fileURLWithPath: CommandLine.arguments[2])
        let entries = [
            ("icp4", "icon_16x16.png"),
            ("icp5", "icon_32x32.png"),
            ("icp6", "icon_32x32@2x.png"),
            ("ic07", "icon_128x128.png"),
            ("ic08", "icon_256x256.png"),
            ("ic09", "icon_512x512.png"),
            ("ic10", "icon_512x512@2x.png")
        ]

        var chunks = Data()
        for (type, filename) in entries {
            let image = try Data(contentsOf: iconset.appendingPathComponent(filename))
            chunks.append(type.data(using: .ascii)!)
            chunks.append(bigEndian: UInt32(image.count + 8))
            chunks.append(image)
        }

        var result = Data("icns".utf8)
        result.append(bigEndian: UInt32(chunks.count + 8))
        result.append(chunks)
        try result.write(to: output)
    }

    enum ICNSError: Error {
        case invalidArguments
    }
}

private extension Data {
    mutating func append(bigEndian value: UInt32) {
        var value = value.bigEndian
        Swift.withUnsafeBytes(of: &value) { bytes in
            append(contentsOf: bytes)
        }
    }
}
