/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The one scheme the app serves, and the reason there is an app at all.
//
// `disc://app/...` carries two things. The engine and its page come out of the bundle, so
// the document is same origin with everything else here and no cross-origin rule applies
// to any of it. `disc://app/disc/<identity>` is a disc image, answered off a file on this
// device -- which is what a browser cannot do, because a page is given a local file only
// asynchronously and the engine's transport is synchronous (code/isohttp.cpp). An image on a
// server is answered too, through the cache in DiscCache.swift, which is where that
// transport's cost against a distant server is dealt with.
//
// What that transport requires is exact: a ranged GET must come back 206 with a
// Content-Range it can parse, and the probe that opens an image reads the total length out
// of that header. A validator is sent with it so a run that fetched blocks under one image
// does not serve them for a different one.

import Foundation
import WebKit
import os

final class DiscSchemeHandler: NSObject, WKURLSchemeHandler {
	static let scheme = "disc"
	static let host = "app"

	/// Bytes delivered out of the images so far. A first run has to fetch a working set
	/// before it can show anything, and over a network that is minutes; this is what the
	/// shell can honestly say is happening while it does.
	private(set) var delivered: UInt64 = 0

	/// The last read that could not be completed at all, if there has been one. A disc that
	/// cannot be read leaves the engine reporting a missing file, which says nothing about
	/// the disk or the network that actually failed.
	///
	/// A read that failed and then succeeded on a later attempt is not recorded here: it
	/// belongs to the log, because nothing was lost and there is nothing to decide.
	private(set) var failure: String?

	private let log = Logger(subsystem: "org.opents.shell", category: "disc")

	/// Reads asked for, and when the run began. A run's cost is the number of round trips it
	/// makes one after another, and neither a byte count nor a wall clock separates a slow
	/// link from a link used one request at a time. The order and the times do.
	///
	/// A mission makes thousands of these, so they are written at debug level and are read
	/// back with `log show --debug`; what reached the network is at info level in the cache's
	/// own category, because that is bounded by the network rather than by the game loop.
	private var asked = 0
	private var began = Date()

	/// Where the engine's page, loader and modules are copied to at build time.
	private let web: URL

	/// One open descriptor per image. An image is read in small blocks, thousands of times
	/// a mission, and opening the file per read would dominate the cost.
	private var handles: [String: FileHandle] = [:]

	/// A task the webview has abandoned must never be written to again: WKWebView raises
	/// NSInternalInconsistencyException, "This task has already been stopped", and the
	/// process dies. Local reads are answered before `start` returns and cannot hit this,
	/// but a remote read lands on a session queue long afterwards.
	private var live = Set<ObjectIdentifier>()
	private let lock = NSLock()

	override init() {
		web = Bundle.main.resourceURL!.appendingPathComponent("web", isDirectory: true)
		super.init()
	}

	// MARK: - WKURLSchemeHandler

	func webView(_ webView: WKWebView, start task: WKURLSchemeTask) {
		lock.lock(); live.insert(ObjectIdentifier(task as AnyObject)); lock.unlock()

		guard let url = task.request.url else { return }
		let path = url.path

		if path.hasPrefix("/disc/") {
			serveDisc(String(path.dropFirst("/disc/".count)), task, url)
		} else {
			serveAsset(path, task, url)
		}
	}

	func webView(_ webView: WKWebView, stop task: WKURLSchemeTask) {
		lock.lock(); live.remove(ObjectIdentifier(task as AnyObject)); lock.unlock()
	}

	// MARK: - Answering

	private func holds(_ task: WKURLSchemeTask) -> Bool {
		lock.lock(); defer { lock.unlock() }
		return live.contains(ObjectIdentifier(task as AnyObject))
	}

	private func answer(_ task: WKURLSchemeTask, _ url: URL, _ status: Int,
	                    _ headers: [String: String], _ body: Data) {
		guard holds(task) else { return }

		let response = HTTPURLResponse(url: url, statusCode: status,
		                               httpVersion: "HTTP/1.1", headerFields: headers)!
		task.didReceive(response)
		task.didReceive(body)
		task.didFinish()

		lock.lock(); live.remove(ObjectIdentifier(task as AnyObject)); lock.unlock()
	}

	private func fail(_ task: WKURLSchemeTask, _ url: URL, _ status: Int) {
		answer(task, url, status, ["Content-Length": "0"], Data())
	}

	/// Records what went wrong, and counts what went right. The first failure is the one
	/// worth showing: a disc that stops answering makes the engine ask again and again,
	/// and the tenth message says no more than the first.
	private func note(read count: Int) {
		lock.lock(); delivered += UInt64(count); lock.unlock()
	}

	private func note(failure text: String) {
		lock.lock()
		if failure == nil { failure = text }
		lock.unlock()
		log.error("\(text, privacy: .public)")
	}

	/// Forgets what a previous run learned and what went wrong in it, so a fresh run is
	/// judged on its own reads rather than on one a restart was meant to leave behind.
	func reset() {
		lock.lock()
		delivered = 0
		failure = nil
		asked = 0
		began = Date()
		lock.unlock()

		DiscCache.shared.reset()
	}

	// MARK: - The bundled engine

	private func serveAsset(_ path: String, _ task: WKURLSchemeTask, _ url: URL) {
		// A page on this origin with no engine on it. Browser storage belongs to an origin
		// and can only be read or emptied from inside one, and doing either underneath a
		// running engine would be reading a store it is writing.
		if path == "/idle" {
			let body = Data("<!doctype html><title>OpenTS</title>".utf8)
			return answer(task, url, 200, [
				"Content-Type": "text/html; charset=utf-8",
				"Content-Length": "\(body.count)",
			], body)
		}

		let name = (path.isEmpty || path == "/") ? "index.html" : String(path.dropFirst())

		// The page and the modules sit flat in one directory; nothing here descends.
		guard !name.contains("/"), !name.hasPrefix(".") else { return fail(task, url, 404) }

		guard let data = try? Data(contentsOf: web.appendingPathComponent(name)) else {
			return fail(task, url, 404)
		}

		// Which module the page settled on is the one thing about a run that cannot be
		// read back afterwards: the choice is made and spent before anything is recorded.
		log.debug("served \(name, privacy: .public)")

		answer(task, url, 200, [
			"Content-Type": Self.mime(name),
			"Content-Length": "\(data.count)",
			"Cache-Control": "no-store",
		], data)
	}

	private static func mime(_ name: String) -> String {
		if name.hasSuffix(".html") { return "text/html; charset=utf-8" }
		if name.hasSuffix(".js") { return "text/javascript; charset=utf-8" }
		if name.hasSuffix(".wasm") { return "application/wasm" }
		if name.hasSuffix(".css") { return "text/css; charset=utf-8" }
		return "application/octet-stream"
	}

	// MARK: - The images

	private func serveDisc(_ identity: String, _ task: WKURLSchemeTask, _ url: URL) {
		guard let disc = DiscLibrary.shared.disc(identity: identity) else {
			return fail(task, url, 404)
		}

		let range = task.request.value(forHTTPHeaderField: "Range")

		lock.lock(); asked += 1; let index = asked; let since = Date().timeIntervalSince(began)
		lock.unlock()
		log.debug("""
			ask #\(index) \(identity, privacy: .public) \(range ?? "whole", privacy: .public) \
			at \(since, format: .fixed(precision: 3))s
			""")

		switch disc.backing {
		case .file(let file):
			serveFile(file, identity: identity, range: range, task, url)
		case .remote(let address):
			serveRemote(address, identity: identity, range: range, task, url)
		}
	}

	/// Answered on the calling thread, which is the app's main thread rather than the web
	/// content process's: the synchronous request the engine is blocked on is blocking a
	/// different process, so a read that returns in microseconds off local storage costs
	/// the window nothing and cannot outlive its task.
	private func serveFile(_ file: URL, identity: String, range: String?,
	                       _ task: WKURLSchemeTask, _ url: URL) {
		guard let handle = handle(identity, file),
		      let attributes = try? FileManager.default.attributesOfItem(atPath: file.path),
		      let total = attributes[.size] as? UInt64 else {
			note(failure: "\(file.lastPathComponent) cannot be opened. It may have been "
			            + "moved, renamed, or be on a disk that is no longer connected.")
			return fail(task, url, 404)
		}

		// A validator the store can tell two images apart by, without reading either.
		let modified = (attributes[.modificationDate] as? Date)?.timeIntervalSince1970 ?? 0
		let tag = "\"\(total)-\(UInt64(modified))\""

		guard let span = Self.parseRange(range, total: total) else {
			// The transport only ever asks for a range; a plain GET of a whole disc image
			// is not something this app has any reason to serve.
			return fail(task, url, range == nil ? 416 : 416)
		}

		var body = Data()
		do {
			try handle.seek(toOffset: span.lowerBound)
			body = try handle.read(upToCount: Int(span.upperBound - span.lowerBound + 1)) ?? Data()
		} catch {
			note(failure: "\(file.lastPathComponent) could not be read: \(error.localizedDescription)")
			return fail(task, url, 500)
		}

		note(read: body.count)
		let last = span.lowerBound + UInt64(body.count) - 1
		answer(task, url, 206, [
			"Content-Type": "application/octet-stream",
			"Accept-Ranges": "bytes",
			"Content-Range": "bytes \(span.lowerBound)-\(last)/\(total)",
			"Content-Length": "\(body.count)",
			"ETag": tag,
			"Cache-Control": "no-store",
		], body)
	}

	private func handle(_ identity: String, _ file: URL) -> FileHandle? {
		if let open = handles[identity] { return open }
		guard let open = try? FileHandle(forReadingFrom: file) else { return nil }
		handles[identity] = open
		return open
	}

	/// Passes the range to the cache in front of the servers, and hands back what it
	/// answered. This is the case a page could do for itself only if the server had said it
	/// could; here no same origin rule and no preflight applies, because the request is not
	/// the page's.
	///
	/// The reason it goes through DiscCache rather than straight to the server is that a
	/// synchronous read cannot overlap another one, so an answer that costs a round trip
	/// costs the whole run a round trip. See DiscCache.swift.
	private func serveRemote(_ address: URL, identity: String, range: String?,
	                         _ task: WKURLSchemeTask, _ url: URL) {
		DiscCache.shared.read(identity: identity, address: address, range: range) {
			[weak self] outcome in
			guard let self, self.holds(task) else { return }

			switch outcome {
			case .failure(let trouble):
				self.note(failure: trouble.text)
				self.fail(task, url, 502)

			case .success(let got):
				guard !got.data.isEmpty else { return self.fail(task, url, 416) }

				self.note(read: got.data.count)
				let last = got.first + UInt64(got.data.count) - 1
				self.answer(task, url, 206, [
					"Content-Type": "application/octet-stream",
					"Accept-Ranges": "bytes",
					"Content-Range": "bytes \(got.first)-\(last)/\(got.total)",
					"Content-Length": "\(got.data.count)",
					"ETag": got.validator,
					"Cache-Control": "no-store",
				], got.data)
			}
		}
	}

	// MARK: - Ranges

	/// Reads the one form of Range header the transport sends: a single closed span.
	static func parseRange(_ text: String?, total: UInt64) -> ClosedRange<UInt64>? {
		guard total > 0, let text, text.hasPrefix("bytes=") else { return nil }

		let span = text.dropFirst("bytes=".count)
		guard let dash = span.firstIndex(of: "-") else { return nil }

		let head = span[span.startIndex..<dash].trimmingCharacters(in: .whitespaces)
		let tail = span[span.index(after: dash)...].trimmingCharacters(in: .whitespaces)

		guard let first = UInt64(head), first < total else { return nil }
		let last = tail.isEmpty ? total - 1 : (UInt64(tail) ?? total - 1)

		guard first <= last else { return nil }
		return first...min(last, total - 1)
	}
}
