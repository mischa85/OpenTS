/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The discs this installation reads, and what it takes to still be able to read them on
// the next launch. OpenTS ships the engine and not the game data, so nothing here names a
// source: the list is empty until a player points the app at one.
//
// A picked file is remembered as a bookmark rather than as a path. On iOS a document
// picker hands out a URL that is only openable while its security scope is held, and a
// path recorded from it is worthless the moment the app is relaunched; on macOS the same
// form keeps working once the app is sandboxed. So both platforms record the bookmark and
// hold the scope for as long as the app runs.

import Foundation
import os

/// Where one disc image is read from.
enum DiscBacking {
	case file(URL)
	case remote(URL)
}

/// One disc, as the page and the scheme handler see it.
struct Disc {
	/// The path segment the page reads this disc through: `disc://app/disc/<identity>`.
	///
	/// It is derived from what the image is rather than from its position in the list,
	/// because the engine's block store is keyed on the location it fetched from. A
	/// stable identity lets a warm run find the blocks a previous one kept, and a
	/// different image lands on a different key instead of inheriting them.
	let identity: String

	/// What the setup screen calls this disc.
	let label: String

	let backing: DiscBacking

	var isRemote: Bool {
		if case .remote = backing { return true }
		return false
	}
}

/// The configured discs, and their persistence.
final class DiscLibrary {
	static let shared = DiscLibrary()

	private static let defaultsKey = "org.opents.discs"

	private(set) var discs: [Disc] = []

	/// Security scoped URLs whose access is held open for the lifetime of the process.
	private var held: [URL] = []

	private init() {
		load()
	}

	var isConfigured: Bool { !discs.isEmpty }

	/// The discs in the order the engine should search them.
	///
	/// Firestorm first: it is the newest of the three, and where it repeats an archive
	/// the base discs also carry, its copy is the one an installed game would run out of.
	/// Beyond that the player's own order stands.
	var searchOrder: [Disc] {
		discs.enumerated()
			.sorted { left, right in
				let a = Self.rank(left.element), b = Self.rank(right.element)
				return a == b ? left.offset < right.offset : a < b
			}
			.map(\.element)
	}

	private static func rank(_ disc: Disc) -> Int {
		let name = disc.label.uppercased()
		return (name.contains("FIRESTORM") || name.contains("FIRE STORM")) ? 0 : 1
	}

	func disc(identity: String) -> Disc? {
		discs.first { $0.identity == identity }
	}

	// MARK: - Editing

	/// Replaces the whole list with the given files. Anything already configured is
	/// dropped, which is what "choose discs" means: the picker shows the full set.
	func setFiles(_ urls: [URL]) {
		var records: [[String: Any]] = []

		for url in urls {
			guard let record = Self.fileRecord(for: url) else { continue }
			records.append(record)
		}

		store(records)
	}

	/// Points one slot at a server. Nothing in the app supplies an address; this exists so
	/// a player who is hosting their own images can say so.
	func setRemote(_ addresses: [URL]) {
		let records = addresses.map { url -> [String: Any] in
			[
				"kind": "remote",
				"url": url.absoluteString,
				"label": url.lastPathComponent.isEmpty ? url.absoluteString : url.lastPathComponent,
				"identity": Self.sanitize(Self.digest(url.absoluteString)),
			]
		}

		store(records)
	}

	func clear() {
		store([])
	}

	private func store(_ records: [[String: Any]]) {
		UserDefaults.standard.set(records, forKey: Self.defaultsKey)
		load()
	}

	// MARK: - Persistence

	private func load() {
		for url in held {
			url.stopAccessingSecurityScopedResource()
		}
		held = []
		discs = []

		let records = UserDefaults.standard.array(forKey: Self.defaultsKey) as? [[String: Any]] ?? []
		var taken = Set<String>()

		for record in records {
			guard let kind = record["kind"] as? String,
			      let label = record["label"] as? String,
			      var identity = record["identity"] as? String else { continue }

			// Two images that describe themselves identically would otherwise share a
			// path, and with it a block store slot.
			while taken.contains(identity) { identity += "-" }
			taken.insert(identity)

			switch kind {
			case "file":
				guard let bookmark = record["bookmark"] as? Data,
				      let url = Self.resolve(bookmark, scoped: record["scoped"] as? Bool ?? false)
				else { continue }
				if url.startAccessingSecurityScopedResource() { held.append(url) }
				discs.append(Disc(identity: identity, label: label, backing: .file(url)))

			case "remote":
				guard let text = record["url"] as? String, let url = URL(string: text) else { continue }
				discs.append(Disc(identity: identity, label: label, backing: .remote(url)))

			default:
				continue
			}
		}

		if discs.isEmpty {
			discs = Self.archiveDiscs()
		}
	}

	/// The discs a first launch plays from, before anything has been configured.
	///
	/// Firestorm leads, as it does everywhere else: the three overlap, and where they do,
	/// its copies are the ones an installation that upgraded over the base game would be
	/// running. Pointing at local files in the settings replaces these.
	private static func archiveDiscs() -> [Disc] {
		let item = "https://archive.org/download/TheCommandConquerCollection/"
		let names = ["FIRESTORM.iso", "TS1.iso", "TS2.iso"]

		return names.compactMap { name in
			guard let url = URL(string: item + name) else { return nil }
			return Disc(identity: name, label: name, backing: .remote(url))
		}
	}

	private static func fileRecord(for url: URL) -> [String: Any]? {
		// The picker's URL is scoped, and the bookmark has to be taken while that scope is
		// held or it records nothing openable.
		let scoped = url.startAccessingSecurityScopedResource()
		defer { if scoped { url.stopAccessingSecurityScopedResource() } }

		guard let bookmark = self.bookmark(for: url) else {
			Logger(subsystem: "org.opents.shell", category: "discs")
				.error("\(url.lastPathComponent, privacy: .public) cannot be remembered for a later launch")
			return nil
		}

		let label = DiscImage.volumeLabel(of: url) ?? url.deletingPathExtension().lastPathComponent
		let size = (try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0

		return [
			"kind": "file",
			"scoped": bookmark.scoped,
			"bookmark": bookmark.data,
			"label": label,
			"identity": sanitize("\(label)-\(size)"),
		]
	}

	/// A bookmark that will still open this file on a later launch.
	///
	/// A security scoped bookmark is the form a sandboxed app has to record, and creating
	/// one is refused unless the app carries a code signing identity the system will grant
	/// a persistent extension to. A build signed only ad hoc, and any unsandboxed build,
	/// records a plain bookmark instead: outside the sandbox the path is reachable anyway,
	/// and inside one there is nothing better to fall back to than saying so at the next
	/// launch and asking again.
	private static func bookmark(for url: URL) -> (data: Data, scoped: Bool)? {
		#if os(macOS)
		if let data = try? url.bookmarkData(options: [.withSecurityScope],
		                                    includingResourceValuesForKeys: nil, relativeTo: nil) {
			return (data, true)
		}
		#endif

		// On iOS the plain form is the only one there is, and a bookmark taken from a
		// document picker's URL resolves back into a scoped one by itself.
		if let data = try? url.bookmarkData(options: [], includingResourceValuesForKeys: nil,
		                                    relativeTo: nil) {
			return (data, false)
		}

		return nil
	}

	private static func resolve(_ bookmark: Data, scoped: Bool) -> URL? {
		var options: URL.BookmarkResolutionOptions = [.withoutUI]
		#if os(macOS)
		if scoped { options.insert(.withSecurityScope) }
		#endif

		var stale = false
		return try? URL(resolvingBookmarkData: bookmark, options: options,
		                relativeTo: nil, bookmarkDataIsStale: &stale)
	}

	// MARK: - Naming

	private static func sanitize(_ text: String) -> String {
		let kept = text.unicodeScalars.map { scalar -> Character in
			let allowed = CharacterSet.alphanumerics.contains(scalar) || "._-".unicodeScalars.contains(scalar)
			return allowed ? Character(scalar) : "-"
		}
		let name = String(kept)
		return name.isEmpty ? "disc" : name
	}

	/// A short stable name for an address, so the store keys on the image rather than on
	/// where in the list the player put it.
	private static func digest(_ text: String) -> String {
		var hash: UInt64 = 0xcbf29ce484222325
		for byte in Array(text.utf8) {
			hash = (hash ^ UInt64(byte)) &* 0x100000001b3
		}
		return String(hash, radix: 36)
	}
}

/// What can be read out of a disc image without mounting it.
enum DiscImage {
	/// The ISO 9660 primary volume descriptor's volume identifier: 32 bytes at sector 16,
	/// offset 40. A file that is not an ISO simply has no readable label here.
	static func volumeLabel(of url: URL) -> String? {
		guard let handle = try? FileHandle(forReadingFrom: url) else { return nil }
		defer { try? handle.close() }

		guard (try? handle.seek(toOffset: 32768 + 40)) != nil,
		      let data = try? handle.read(upToCount: 32), data.count == 32 else { return nil }

		let text = String(decoding: data, as: UTF8.self)
			.trimmingCharacters(in: CharacterSet(charactersIn: " \0"))

		return text.isEmpty ? nil : text
	}
}
