/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// What stands between the engine's reads and a disc image on the far side of the internet.
//
// The engine reads through a synchronous request, and WebKit answers a synchronous request
// on a custom scheme by blocking the web content process on one IPC to this one
// (WebLoaderStrategy::loadResourceSynchronously, then WebURLSchemeHandlerProxy::
// loadSynchronously). So a read costs whatever this shell takes to answer it, and nothing
// the engine does can overlap two of them. Against a public mirror that is the whole
// problem: measured against archive.org a ranged GET waits 1.1-2.9 s for its first byte
// almost regardless of how much was asked for, and the run to the menu touches 124 blocks
// spread across three images. One after another, that is a quarter of an hour.
//
// The engine's own read-ahead is not at fault. It sizes its window by the bandwidth-delay
// product it can measure, and all it can measure through that transport is a 64 KiB request
// that took two seconds -- which reads as a link delivering 27 KB/s rather than as a fast
// link two seconds away. The product comes out at about one block and the window shuts.
// This file measures the same link where the two can be told apart, and acts on it: it asks
// for a megabyte at a time, because a megabyte costs what a block costs, and it asks for
// several at once, because eight beside each other cost what one costs.
//
// So a read that misses is still one round trip, but it is the only one the engine waits
// for: the blocks around and in front of it are fetched beside it and are here by the time
// they are asked for. What arrives is kept on this device, so a round trip is paid once
// ever rather than once per launch.
//
// Nothing here knows what a disc image holds. No offset, block or region is named: what is
// kept is whatever was read, and what is guessed at is the megabyte a read landed in and the
// ones after it. So it is the same cache for an image of any language, edition or layout,
// and an image whose files sit somewhere else costs it a different set of guesses rather
// than a wrong one.
//
// None of this stands in front of a local image. Those are read straight off the disk by
// DiscSchemeHandler.serveFile and never reach this file; a cache over them would be a second
// copy of something already at hand.

import Foundation
import os

/// The disc images this device is holding on behalf of ones that are read over a network.
final class DiscCache {
	static let shared = DiscCache()

	/// What presence is recorded in: the engine's own block size, `ISO_BLOCK_SIZE` in
	/// code/isohttp.h. Recording anything finer would describe a read the engine never makes.
	static let block: UInt64 = 65536

	/// Blocks one speculative request asks for. A ranged request to a public mirror costs
	/// what it costs before it carries anything: measured against archive.org, 64 KiB comes
	/// back in about 1.4 s and 1 MiB in about 2.4 s, so sixteen blocks is about the most
	/// that can be taken before the transfer starts to show against the wait.
	///
	/// Larger is not better, and this was measured rather than reasoned. Two megabyte units
	/// cover a cold start in 22 rather than 27 of them, so they should leave fewer round
	/// trips to pay for; they do not. They take twice as long to arrive, so the read that
	/// wanted the second half of one has already missed and asked again by the time it
	/// lands, and they crowd the reads the engine is actually blocked on. The run to the
	/// menu came out at 28 misses and 59 s of blocked reading against 27 and 45 s.
	static let unit: UInt64 = 16

	/// Units asked for around a read that missed, the one holding it included. Four of them
	/// is what a link at a mirror's measured rate carries in the time one round trip costs;
	/// asking further ahead would arrive after the read that wanted it.
	static let reach: UInt64 = 4

	/// Speculative requests one image may have outstanding. A read the engine is waiting on
	/// is never queued behind these, so this bounds how much of the link the guessing takes
	/// rather than how long a read waits.
	static let ahead = 6

	/// How many times a read is asked for before it is called a failure. A cold start makes
	/// hundreds of these, and a public mirror answering one of them badly is ordinary rather
	/// than evidence about the mirror.
	static let attempts = 3

	/// The most the whole cache may occupy. Three disc images read from end to end come to
	/// about two gigabytes, so this is "as much as the discs themselves" and no more; over
	/// it, whole images are dropped, least recently used first.
	static let limit: UInt64 = 2 << 30

	private let log = Logger(subsystem: "org.opents.shell", category: "cache")
	private let lock = NSLock()

	/// One record per image this run has touched, by the identity the page reads it through.
	private var discs: [String: Held] = [:]

	/// Requests out to the network right now. What a run costs is the number of round trips
	/// it makes one after another, and neither a byte count nor a wall clock separates a
	/// slow link from a link used one request at a time. This does.
	private(set) var inflight = 0

	/// Bytes the network delivered, and how many of those nothing asked for. The second is
	/// what the guessing costs and is worth being able to see.
	private(set) var fetched: UInt64 = 0
	private(set) var speculated: UInt64 = 0

	private lazy var session: URLSession = {
		let configuration = URLSessionConfiguration.ephemeral
		configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
		// The point of the whole file. A mirror serving HTTP/2 multiplexes these onto one
		// connection and one serving HTTP/1.1 opens more; either way eight ranged reads
		// beside each other cost about what one of them costs, and that is where the time
		// comes back.
		configuration.httpMaximumConnectionsPerHost = 8
		configuration.timeoutIntervalForRequest = 60
		return URLSession(configuration: configuration)
	}()

	/// Where the images are kept. `Caches` rather than `Application Support` because every
	/// byte of it can be fetched again: the system may reclaim the directory under disk
	/// pressure, and a run that loses it pays for what it lost and nothing else.
	private lazy var home: URL = {
		let base = (try? FileManager.default.url(for: .cachesDirectory, in: .userDomainMask,
		                                         appropriateFor: nil, create: true))
			?? URL(fileURLWithPath: NSTemporaryDirectory())
		let directory = base.appendingPathComponent("Discs", isDirectory: true)
		try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
		return directory
	}()

	// MARK: - What the shell asks of it

	/// Why a read could not be answered, said in what the server actually did. It is carried
	/// as far as the alert the player sees, so it is a sentence rather than a code.
	struct Trouble: Error {
		let text: String
	}

	/// What one read came back with, in the shape a 206 is built out of.
	struct Answer {
		var data: Data
		var first: UInt64
		var total: UInt64
		var validator: String
	}

	/// Opens the run's remote images before the engine asks for them.
	///
	/// The engine opens an image with a one byte probe and then reads its first block, once
	/// per image and through the synchronous transport -- six round trips one after another,
	/// each of the first three taking a fresh redirect, before a byte of game data is read.
	/// Here that is one round trip: the images are opened beside each other while the module
	/// is still compiling, and what the probes want is already on this device.
	///
	/// It is also where a kept image is checked against the server. The answer carries the
	/// length and the validator, so an image that has been replaced is noticed once per run
	/// instead of being served stale out of the cache.
	func prime(_ discs: [Disc]) {
		let remote = discs.compactMap { disc -> (String, URL)? in
			guard case .remote(let address) = disc.backing else { return nil }
			return (disc.identity, address)
		}
		guard !remote.isEmpty else { return }

		reclaim(keeping: Set(remote.map(\.0)))

		for (identity, address) in remote {
			lock.lock()
			let held = record(identity, address)
			let start = !held.opening && !held.ready
			held.opening = held.opening || start
			lock.unlock()

			if start { open(held) }
		}
	}

	/// Answers a ranged read of a remote image, off this device wherever it can be.
	func read(identity: String, address: URL, range: String?,
	          done: @escaping (Result<Answer, Trouble>) -> Void) {
		lock.lock()
		let held = record(identity, address)

		// Nothing kept may be served before the image has been checked against the server
		// this run: what is here has not been shown to be what the server still has. There
		// is at most one such wait per image, and it is the round trip the engine's own
		// probe would have cost anyway.
		guard held.ready else {
			held.waiting.append { [weak self] why in
				if let why { return done(.failure(Trouble(text: why))) }
				self?.read(identity: identity, address: address, range: range, done: done)
			}
			let start = !held.opening
			held.opening = true
			lock.unlock()
			if start { open(held) }
			return
		}

		let total = held.total
		guard let span = DiscSchemeHandler.parseRange(range, total: total) else {
			lock.unlock()
			return done(.failure(Trouble(text: "\(address.lastPathComponent) was asked for a "
			                                 + "range the image does not have.")))
		}

		if let data = held.take(span) {
			let answer = Answer(data: data, first: span.lowerBound,
			                    total: total, validator: held.validator)
			lock.unlock()
			guess(held, around: span.lowerBound)
			return done(.success(answer))
		}
		lock.unlock()

		// The read the engine is blocked on asks for exactly what was wanted and nothing
		// more, so that it waits for as little as a request can carry. The megabyte it sits
		// in, and the ones after it, are asked for beside it rather than in front of it.
		let blocks = (span.lowerBound / Self.block)...(span.upperBound / Self.block)
		guess(held, around: span.lowerBound)

		request(held, blocks: blocks, retry: true, attempt: 1) { [weak self] outcome in
			guard let self else { return }

			switch outcome {
			case .failure(let why):
				done(.failure(why))

			case .success(let arrived):
				self.lock.lock()
				held.adopt(total: arrived.total, validator: arrived.validator, log: self.log)
				held.put(blocks: blocks, data: arrived.data)
				self.fetched += UInt64(arrived.data.count)
				let validator = held.validator
				self.lock.unlock()

				// Answered out of what arrived rather than read back out of the file: the
				// bytes are in hand, and a cache that could not be written is then still a
				// run that plays.
				let from = Int(span.lowerBound - blocks.lowerBound * Self.block)
				let length = Int(span.upperBound - span.lowerBound + 1)
				guard from >= 0, arrived.data.count >= from else {
					return done(.failure(Trouble(text: "\(address.lastPathComponent) answered a "
					                                 + "range without the bytes that were "
					                                 + "asked for.")))
				}

				let end = min(from + length, arrived.data.count)
				done(.success(Answer(data: arrived.data.subdata(in: from..<end),
				                     first: span.lowerBound, total: arrived.total,
				                     validator: validator)))
			}
		}
	}

	/// Bytes the cache occupies on this device, which for the sparse images it keeps is what
	/// has been written into them rather than how long they are.
	func occupied() -> UInt64 {
		var total: UInt64 = 0
		for name in kept() { total += Self.allocated(name) }
		return total
	}

	/// Empties the cache. Everything in it can be fetched again, so what is lost is the time
	/// the next run spends fetching it and nothing else.
	func empty() {
		lock.lock()
		for held in discs.values { held.close() }
		discs = [:]
		lock.unlock()

		for name in kept() { try? FileManager.default.removeItem(at: name) }
	}

	/// Records everything that has arrived but is not yet written down. Called when the app
	/// is going away, so that a quit costs at most the blocks of the last two seconds.
	func flush() {
		lock.lock()
		for held in discs.values { held.flush() }
		lock.unlock()
	}

	/// Forgets what a previous run learned, so a new one is judged on its own reads. What is
	/// on this device stays, which is the point of it.
	func reset() {
		lock.lock()
		for held in discs.values { held.close() }
		discs = [:]
		inflight = 0
		fetched = 0
		speculated = 0
		lock.unlock()
	}

	private func kept() -> [URL] {
		let names = (try? FileManager.default.contentsOfDirectory(
			at: home, includingPropertiesForKeys: [.contentAccessDateKey, .contentModificationDateKey])) ?? []
		return names.filter { $0.pathExtension == "disc" }
	}

	// MARK: - Opening an image

	private func record(_ identity: String, _ address: URL) -> Held {
		if let held = discs[identity] { return held }
		let held = Held(identity: identity, address: address,
		                path: home.appendingPathComponent(identity + ".disc"))
		discs[identity] = held
		return held
	}

	/// Asks the server what the image is, and settles what of the kept copy may be used.
	private func open(_ held: Held) {
		let first = UInt64(0)...(Self.unit - 1)

		request(held, blocks: first, retry: true, attempt: 1) { [weak self] outcome in
			guard let self else { return }

			var waiting: [(String?) -> Void] = []
			var why: String?

			self.lock.lock()
			switch outcome {
			case .failure(let trouble):
				why = trouble.text
			case .success(let arrived):
				held.adopt(total: arrived.total, validator: arrived.validator, log: self.log)
				held.put(blocks: first, data: arrived.data)
				self.fetched += UInt64(arrived.data.count)
				held.ready = true
			}
			held.opening = false
			waiting = held.waiting
			held.waiting = []
			let have = held.have, count = held.count
			self.lock.unlock()

			if let why {
				self.log.error("\(held.identity, privacy: .public) could not be opened: \(why, privacy: .public)")
			} else {
				self.log.info("""
					\(held.identity, privacy: .public) opened: \(have) of \(count) blocks \
					were already here
					""")
			}

			for resume in waiting { resume(why) }
		}
	}

	// MARK: - Guessing

	/// Asks for the megabytes around and in front of a read that missed, on whatever of the
	/// link the read itself is not using.
	///
	/// Around, and not only in front: the engine opens a file by reading near its start and
	/// then just before that, so the unit a read landed in is worth having whole before the
	/// ones after it are.
	private func guess(_ held: Held, around first: UInt64) {
		let unit = Self.unit
		let base = (first / Self.block / unit) * unit

		lock.lock(); let count = held.count; lock.unlock()

		for step in 0..<Self.reach {
			let start = base + step * unit
			guard start < count else { break }
			let stop = min(start + unit, count) - 1

			lock.lock()
			let take = inflight < Self.ahead + 2 && held.speculating < Self.ahead
				&& held.claim(start...stop)
			if take { held.speculating += 1 }
			lock.unlock()

			guard take else { continue }

			request(held, blocks: start...stop, retry: false, attempt: 1) { [weak self] outcome in
				guard let self else { return }

				self.lock.lock()
				held.release(start...stop)
				held.speculating -= 1
				if case .success(let arrived) = outcome {
					held.adopt(total: arrived.total, validator: arrived.validator, log: self.log)
					held.put(blocks: start...stop, data: arrived.data)
					self.fetched += UInt64(arrived.data.count)
					self.speculated += UInt64(arrived.data.count)
				}
				self.lock.unlock()
			}
		}
	}

	// MARK: - The network

	private struct Arrival {
		var data: Data
		var total: UInt64
		var validator: String
	}

	/// One ranged GET, tried again while it is a read something is waiting on.
	///
	/// A read that recovers goes to the log and nowhere else. Only a read that stays failed
	/// is reported, and what is reported is what the server answered rather than what that
	/// would imply about the server. A guess that does not arrive is not reported at all:
	/// the blocks simply stay absent, and whoever wants them asks for them.
	private func request(_ held: Held, blocks: ClosedRange<UInt64>, retry: Bool, attempt: Int,
	                     done: @escaping (Result<Arrival, Trouble>) -> Void) {
		lock.lock()
		let target = held.pinned ?? held.address
		let total = held.total
		lock.unlock()

		let first = blocks.lowerBound * Self.block
		let past = (blocks.upperBound + 1) * Self.block
		let header = "bytes=\(first)-\((total > 0 ? min(past, total) : past) - 1)"

		var out = URLRequest(url: target)
		out.cachePolicy = .reloadIgnoringLocalCacheData
		out.setValue(header, forHTTPHeaderField: "Range")

		let sent = Date()
		lock.lock(); inflight += 1; let busy = inflight; lock.unlock()

		session.dataTask(with: out) { [weak self] data, response, error in
			guard let self else { return }

			self.lock.lock(); self.inflight -= 1; self.lock.unlock()

			let http = response as? HTTPURLResponse
			let name = held.address.lastPathComponent

			// What the transport requires of a ranged read is exact: a partial answer with
			// a Content-Range the image's length can be read out of. Anything else is a
			// read that did not happen. What one answer says about the host is not knowable
			// from it, so what is recorded is what arrived and nothing beyond it.
			var wrong: String?
			var length: UInt64 = 0

			if let http {
				if http.statusCode != 206 {
					wrong = "the server answered \(http.statusCode) where a partial "
					      + "response (206) was required"
				} else if let range = http.value(forHTTPHeaderField: "Content-Range"),
				          let slash = range.lastIndex(of: "/"),
				          let count = UInt64(range[range.index(after: slash)...]) {
					length = count
				} else {
					wrong = "the server answered without a Content-Range its length could "
					      + "be read out of"
				}
			} else {
				wrong = error?.localizedDescription ?? "the request did not complete"
			}

			self.log.info("""
				\(held.identity, privacy: .public) \(header, privacy: .public) \
				\(retry ? "read" : "ahead", privacy: .public) \
				status \(http?.statusCode ?? -1) bytes \(data?.count ?? 0) \
				in \(Date().timeIntervalSince(sent), format: .fixed(precision: 3))s \
				of \(busy) in flight
				""")

			if let wrong {
				// A pin is a guess at which node of a mirror to keep asking. A read that
				// failed is reason enough to stop guessing and resolve the address afresh.
				self.lock.lock(); held.pinned = nil; self.lock.unlock()

				guard retry, attempt < Self.attempts else {
					return done(.failure(Trouble(text: "\(name) could not be read from "
						+ "\(held.address.host ?? held.address.absoluteString). It was asked "
						+ "\(retry ? Self.attempts : 1) times, and \(wrong).")))
				}

				self.log.notice("""
					\(name, privacy: .public) \(header, privacy: .public): \
					\(wrong, privacy: .public); attempt \(attempt) of \(Self.attempts)
					""")

				let when = DispatchTime.now() + .milliseconds(250 * attempt)
				DispatchQueue.global(qos: .userInitiated).asyncAfter(deadline: when) {
					self.request(held, blocks: blocks, retry: retry, attempt: attempt + 1,
					             done: done)
				}
				return
			}

			if let settled = http?.url, settled != target {
				self.lock.lock()
				if held.pinned == nil { held.pinned = settled }
				self.lock.unlock()
			}

			if attempt > 1 {
				self.log.notice("\(name, privacy: .public) \(header, privacy: .public): read on attempt \(attempt)")
			}

			done(.success(Arrival(data: data ?? Data(), total: length,
			                      validator: http?.value(forHTTPHeaderField: "ETag")
				                      ?? http?.value(forHTTPHeaderField: "Last-Modified") ?? "")))
		}.resume()
	}

	// MARK: - Bounding what is kept

	/// Drops whole images, least recently used first, until the cache is inside its limit.
	///
	/// Whole images rather than parts of them: an image is either one of the discs a player
	/// is using or one they have stopped using, and taking the middle out of one they are
	/// using would buy back a little disk at the cost of round trips in mid-mission.
	private func reclaim(keeping: Set<String>) {
		var names = kept()

		var total: UInt64 = 0
		for name in names { total += Self.allocated(name) }
		guard total > Self.limit else { return }

		names.sort { Self.used($0) < Self.used($1) }

		for name in names {
			guard total > Self.limit else { break }
			guard !keeping.contains(name.deletingPathExtension().lastPathComponent) else { continue }

			let size = Self.allocated(name)
			guard (try? FileManager.default.removeItem(at: name)) != nil else { continue }
			total -= min(size, total)
			log.notice("""
				dropped the kept copy of \(name.lastPathComponent, privacy: .public) to stay \
				inside the cache limit
				""")
		}
	}

	private static func used(_ url: URL) -> Date {
		let values = try? url.resourceValues(forKeys: [.contentAccessDateKey,
		                                               .contentModificationDateKey])
		return values?.contentAccessDate ?? values?.contentModificationDate ?? .distantPast
	}

	/// What a file takes on this device, which for a sparse image is much less than how long
	/// it is.
	private static func allocated(_ url: URL) -> UInt64 {
		var status = stat()
		guard stat(url.path, &status) == 0 else { return 0 }
		return UInt64(status.st_blocks) * 512
	}
}

/// One image, the part of it that is on this device, and the file that holds it.
///
/// The file is sparse and as long as the image, with the blocks that have arrived written
/// where they belong and a bitmap in its header saying which those are. One file rather than
/// a directory of blocks, so that the bitmap and the bytes it describes cannot be reclaimed
/// separately and so that a block costs a seek rather than an open.
private final class Held {
	let identity: String
	let address: URL
	let path: URL

	/// The node a redirecting host settled on, kept so a run does not take a fresh redirect
	/// for every request.
	var pinned: URL?

	var total: UInt64 = 0
	var validator = ""

	/// Whether the image has been checked against the server this run. Nothing kept may be
	/// served before it has been.
	var ready = false
	var opening = false
	var waiting: [(String?) -> Void] = []

	/// Speculative requests out for this image, and the blocks they cover, so two guesses
	/// do not ask for the same megabyte.
	var speculating = 0
	private var claimed = Set<UInt64>()

	private var fd: Int32 = -1
	private var bits: [UInt8] = []
	private var dirty = false
	private var flushed = Date.distantPast

	/// Set once the record is finished with. A request that was still out when a run ended
	/// must not reopen the file underneath the record the next run is using it through.
	private var done = false

	/// The header is a fixed span so that block *n* sits at a fixed offset whatever the
	/// bitmap grows to. Sixty-four kilobytes of it describes an image of thirty gigabytes.
	private static let header: UInt64 = 65536
	private static let magic = "OPENTS-DISC-1\0\0\0"

	init(identity: String, address: URL, path: URL) {
		self.identity = identity
		self.address = address
		self.path = path
	}

	var count: UInt64 { total == 0 ? 0 : (total + DiscCache.block - 1) / DiscCache.block }

	var have: UInt64 {
		var found: UInt64 = 0
		for byte in bits { found += UInt64(byte.nonzeroBitCount) }
		return found
	}

	// MARK: - The file

	/// Takes on what the server says the image is, keeping what is already here only if it
	/// was fetched from the same one.
	func adopt(total: UInt64, validator: String, log: Logger) {
		guard total > 0, !done else { return }

		if fd >= 0 {
			guard total != self.total || (!validator.isEmpty && validator != self.validator) else {
				return
			}
			// A server answering differently is serving a different file, and what is here
			// describes the old one.
			log.notice("""
				\(self.identity, privacy: .public) has changed on the server; the kept copy \
				is dropped
				""")
			close()
			try? FileManager.default.removeItem(at: path)
		}

		self.total = total
		self.validator = validator
		bits = [UInt8](repeating: 0, count: Int((count + 7) / 8))

		fd = Darwin.open(path.path, O_RDWR | O_CREAT, 0o600)
		guard fd >= 0 else { return }

		if let kept = readHeader(), kept.total == total,
		   validator.isEmpty || kept.validator == validator {
			bits = kept.bits
			self.validator = kept.validator
			return
		}

		// Whatever is in the file describes some other image. Started again rather than
		// truncated to the same length, so the space it was taking goes back to the disk.
		Darwin.close(fd)
		try? FileManager.default.removeItem(at: path)
		fd = Darwin.open(path.path, O_RDWR | O_CREAT, 0o600)
		guard fd >= 0 else { return }
		ftruncate(fd, off_t(Self.header + total))
		writeHeader()
	}

	func close() {
		flush()
		if fd >= 0 { Darwin.close(fd) }
		fd = -1
		done = true
	}

	private func readHeader() -> (total: UInt64, validator: String, bits: [UInt8])? {
		var head = [UInt8](repeating: 0, count: Int(Self.header))
		let read = head.withUnsafeMutableBytes { pread(fd, $0.baseAddress, Int(Self.header), 0) }
		guard read == Int(Self.header) else { return nil }
		guard String(decoding: head[0..<16], as: UTF8.self) == Self.magic else { return nil }

		let total = UInt64(littleEndian: head.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 16, as: UInt64.self) })
		let size = UInt32(littleEndian: head.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 24, as: UInt32.self) })
		let length = Int(UInt32(littleEndian: head.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 28, as: UInt32.self) }))

		guard UInt64(size) == DiscCache.block, length <= 256, total > 0 else { return nil }

		let width = Int((((total + DiscCache.block - 1) / DiscCache.block) + 7) / 8)
		guard 1024 + width <= Int(Self.header) else { return nil }

		return (total, String(decoding: head[32..<(32 + length)], as: UTF8.self),
		        Array(head[1024..<(1024 + width)]))
	}

	private func writeHeader() {
		guard fd >= 0 else { return }

		var head = [UInt8](repeating: 0, count: Int(Self.header))
		head.replaceSubrange(0..<16, with: Array(Self.magic.utf8))
		withUnsafeBytes(of: total.littleEndian) { head.replaceSubrange(16..<24, with: $0) }
		withUnsafeBytes(of: UInt32(DiscCache.block).littleEndian) {
			head.replaceSubrange(24..<28, with: $0)
		}

		let tag = Array(validator.utf8.prefix(256))
		withUnsafeBytes(of: UInt32(tag.count).littleEndian) { head.replaceSubrange(28..<32, with: $0) }
		if !tag.isEmpty { head.replaceSubrange(32..<(32 + tag.count), with: tag) }
		if !bits.isEmpty { head.replaceSubrange(1024..<(1024 + bits.count), with: bits) }

		_ = head.withUnsafeBytes { pwrite(fd, $0.baseAddress, Int(Self.header), 0) }
	}

	/// Records the arrived blocks, but only once their bytes are durably on this device.
	///
	/// The order is the whole of what keeps the cache honest across a crash: a bit is
	/// written after the bytes it describes have been flushed, so what a crash can lose is a
	/// block that has to be fetched again, and never a block that reads as present and is
	/// zeroes.
	func flush() {
		guard fd >= 0, dirty else { return }
		fsync(fd)
		writeHeader()
		dirty = false
		flushed = Date()
	}

	// MARK: - Blocks

	private func has(_ index: UInt64) -> Bool {
		let byte = Int(index / 8)
		guard byte < bits.count else { return false }
		return bits[byte] & (1 << UInt8(index % 8)) != 0
	}

	/// Takes a span of blocks for one speculative request, or reports that it is not worth
	/// asking for: another request already covers part of it, or it is all here.
	func claim(_ blocks: ClosedRange<UInt64>) -> Bool {
		var wanted = false
		for index in blocks {
			if claimed.contains(index) { return false }
			if !has(index) { wanted = true }
		}
		guard wanted else { return false }

		for index in blocks { claimed.insert(index) }
		return true
	}

	func release(_ blocks: ClosedRange<UInt64>) {
		for index in blocks { claimed.remove(index) }
	}

	/// Reads a span out of the kept copy, or reports that not all of it is here.
	func take(_ span: ClosedRange<UInt64>) -> Data? {
		guard fd >= 0 else { return nil }

		var index = span.lowerBound / DiscCache.block
		let last = span.upperBound / DiscCache.block
		while index <= last {
			guard has(index) else { return nil }
			index += 1
		}

		let length = Int(span.upperBound - span.lowerBound + 1)
		var data = Data(count: length)
		let read = data.withUnsafeMutableBytes {
			pread(fd, $0.baseAddress, length, off_t(Self.header + span.lowerBound))
		}
		return read == length ? data : nil
	}

	/// Writes what arrived where it belongs. A short answer is written as far as it goes and
	/// only the blocks it filled are recorded.
	func put(blocks: ClosedRange<UInt64>, data: Data) {
		guard fd >= 0, !data.isEmpty, total > 0 else { return }

		let at = blocks.lowerBound * DiscCache.block
		guard at < total else { return }

		let length = min(data.count, Int(total - at))
		let written = data.withUnsafeBytes {
			pwrite(fd, $0.baseAddress, length, off_t(Self.header + at))
		}
		guard written > 0 else { return }

		let reached = at + UInt64(written)
		for index in blocks {
			let stop = min((index + 1) * DiscCache.block, total)
			guard stop <= reached else { break }
			let byte = Int(index / 8)
			guard byte < bits.count else { break }
			bits[byte] |= (1 << UInt8(index % 8))
		}

		dirty = true
		if Date().timeIntervalSince(flushed) > 2 { flush() }
	}
}
