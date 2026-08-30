/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Renders the app icons from the project's own mark, manual/site/public/favicon.svg, and
// writes the two asset catalogs. Run it when the mark changes:
//
//     swift apple/Support/make-icons.swift
//
// The rendered PNGs are checked in so that a build needs nothing but Xcode. This is the
// one place they come from; nothing else should redraw the mark.
//
// The two platforms want the same artwork differently. macOS puts every icon inside the
// system's rounded shape with air around it, so the mark is inset and the corners outside
// it stay clear. iOS masks the icon itself and rejects an alpha channel, so there the mark
// fills the square and is flattened onto its own background colour.

import AppKit

let root = URL(fileURLWithPath: #filePath)
	.deletingLastPathComponent()   // Support
	.deletingLastPathComponent()   // apple
	.deletingLastPathComponent()   // repository root

let mark = root.appendingPathComponent("apple/Support/icon.png")

guard let source = NSImage(contentsOf: mark) else {
	FileHandle.standardError.write(Data("cannot read \(mark.path)\n".utf8))
	exit(1)
}

/// What stands behind the mark where iOS will not accept transparency. The mark is drawn
/// for a pale page, so it is given one rather than the near black the game itself uses.
let ground = NSColor(srgbRed: 0xf3 / 255.0, green: 0xf2 / 255.0, blue: 0xef / 255.0, alpha: 1)

func render(_ size: Int, inset: CGFloat, opaque: Bool) -> Data {
	let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size,
	                           bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
	                           isPlanar: false, colorSpaceName: .deviceRGB,
	                           bytesPerRow: 0, bitsPerPixel: 0)!

	NSGraphicsContext.saveGraphicsState()
	NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)

	let whole = NSRect(x: 0, y: 0, width: size, height: size)
	if opaque {
		ground.setFill()
		whole.fill()
	}

	let margin = CGFloat(size) * inset
	source.draw(in: whole.insetBy(dx: margin, dy: margin), from: .zero,
	            operation: .sourceOver, fraction: 1)

	NSGraphicsContext.restoreGraphicsState()

	// iOS refuses an icon carrying an alpha channel, so the opaque form is redrawn into a
	// buffer that has no alpha to write. Core Graphics has no 24 bit context, so the fourth
	// byte stays and is declared unused rather than being a channel.
	guard opaque else {
		return rep.representation(using: .png, properties: [.interlaced: false])!
	}

	let context = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8,
	                        bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(),
	                        bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!
	context.draw(rep.cgImage!, in: CGRect(x: 0, y: 0, width: size, height: size))

	return NSBitmapImageRep(cgImage: context.makeImage()!)
		.representation(using: .png, properties: [.interlaced: false])!
}

func write(_ catalog: URL, _ images: [(name: String, json: String)],
           _ render: (String) -> Data) throws {
	try FileManager.default.createDirectory(at: catalog, withIntermediateDirectories: true)

	for image in images {
		try render(image.name).write(to: catalog.appendingPathComponent(image.name))
	}

	let entries = images.map(\.json).joined(separator: ",\n")
	let contents = """
	{
	  "images" : [
	\(entries)
	  ],
	  "info" : { "author" : "opents", "version" : 1 }
	}

	"""
	try Data(contents.utf8).write(to: catalog.appendingPathComponent("Contents.json"))
}

// MARK: - macOS

let macSizes: [(Int, Int)] = [(16, 1), (16, 2), (32, 1), (32, 2), (128, 1), (128, 2),
                              (256, 1), (256, 2), (512, 1), (512, 2)]

let macImages = macSizes.map { point, scale -> (String, String) in
	let pixels = point * scale
	return ("icon_\(pixels).png", """
	    {
	      "filename" : "icon_\(pixels).png",
	      "idiom" : "mac",
	      "scale" : "\(scale)x",
	      "size" : "\(point)x\(point)"
	    }
	""")
}

var macRendered: [Int: Data] = [:]
try write(root.appendingPathComponent("apple/macOS/Assets.xcassets/AppIcon.appiconset"),
          macImages) { name in
	let pixels = Int(name.dropFirst("icon_".count).dropLast(".png".count))!
	if let done = macRendered[pixels] { return done }
	let data = render(pixels, inset: 0.10, opaque: false)
	macRendered[pixels] = data
	return data
}

// MARK: - iOS

try write(root.appendingPathComponent("apple/iOS/Assets.xcassets/AppIcon.appiconset"),
          [("icon_1024.png", """
	    {
	      "filename" : "icon_1024.png",
	      "idiom" : "universal",
	      "platform" : "ios",
	      "size" : "1024x1024"
	    }
	""")]) { _ in render(1024, inset: 0.08, opaque: true) }

print("icons written from \(mark.lastPathComponent)")
