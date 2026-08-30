/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The engine, and the one webview it runs in. Both platforms build the same session; what
// differs between them is only the window it is put in and how a player names their discs.
//
// The page is served from the same scheme as the images rather than from a file URL, which
// is what makes them same origin. It also makes the document a secure context, so the
// browser storage the engine keeps its saved games and its fetched blocks in is available.

import Foundation
import WebKit
import os

#if os(macOS)
import AppKit
#else
import UIKit
#endif

final class GameSession: NSObject {
	static let shared = GameSession()

	private(set) var webView: GameWebView!

	/// Whether the page shows its own diagnostic overlay. It is a diagnostic and not part
	/// of the game, so it is off unless asked for, and the choice survives a relaunch.
	var diagnostics: Bool {
		get { UserDefaults.standard.bool(forKey: "org.opents.diagnostics") }
		set { UserDefaults.standard.set(newValue, forKey: "org.opents.diagnostics") }
	}

	private let log = Logger(subsystem: "org.opents.shell", category: "page")

	/// The engine's record of the reads it was blocked on, one line per stall.
	///
	/// At notice level rather than info or debug, which is what makes it readable after the
	/// fact: those two live in the memory buffer and a session that has ended may no longer
	/// have them, and this record exists to be read once the session is over.
	///
	///     log show --predicate 'process == "OpenTS" and subsystem == "org.opents.shell" \
	///         and category == "stall"' --last 1h
	private let stalls = Logger(subsystem: "org.opents.shell", category: "stall")

	private let handler = DiscSchemeHandler()

	private override init() {
		super.init()

		let configuration = WKWebViewConfiguration()
		configuration.setURLSchemeHandler(handler, forURLScheme: DiscSchemeHandler.scheme)

		// The engine starts its own audio and resumes it on the first gesture; nothing
		// here should make it wait for a gesture it will never see as a click on a page.
		configuration.mediaTypesRequiringUserActionForPlayback = []

		configuration.userContentController.add(self, name: "log")

		#if os(iOS)
		configuration.allowsInlineMediaPlayback = true
		#endif

		webView = GameWebView(frame: .zero, configuration: configuration)
		webView.navigationDelegate = self
		webView.isInspectable = true

		#if os(macOS)
		webView.allowsMagnification = false
		#else
		webView.scrollView.isScrollEnabled = false
		webView.scrollView.bounces = false
		webView.scrollView.contentInsetAdjustmentBehavior = .never
		webView.isOpaque = false
		#endif
	}

	// MARK: - Running

	/// The address the engine is started at. The discs are named through the page's own
	/// `?image=` switch, so the shell configures the run the same way a URL does and the
	/// page needs nothing that only this app can give it.
	var pageURL: URL {
		var components = URLComponents()
		components.scheme = DiscSchemeHandler.scheme
		components.host = DiscSchemeHandler.host
		components.path = "/index.html"

		var query = DiscLibrary.shared.searchOrder.map {
			URLQueryItem(name: "image", value: "disc/\($0.identity)")
		}
		if diagnostics { query.append(URLQueryItem(name: "hud", value: "on")) }
		components.queryItems = query

		return components.url!
	}

	/// What the shell can say about a run without asking the engine to stop and answer.
	struct Status {
		var started = false
		var frames = 0
		/// Frames per second over the last sample, once the engine is drawing.
		var rate = 0.0
		/// Bytes read out of the images. On a first run this is the whole of what is
		/// happening for as long as it takes to reach the menu.
		var delivered: UInt64 = 0
		var failure: String?
	}

	/// Called about twice a second while a run is loaded.
	var onStatus: ((Status) -> Void)?

	private var poll: Timer?
	private var lastFrames = 0
	private var lastSample = Date()
	private var sinceLog = 0

	/// When this run was started, and whether it has drawn yet. What a cold start costs is
	/// the wall clock from the load to the first frame, and nothing else the shell records
	/// measures it.
	private var runBegan = Date()
	private var drawn = false

	/// Where the stall record has got to. The sequence number is the engine's, so a ring
	/// that wrapped shows up as a gap rather than as a record that quietly lost entries.
	private var stallSeq: Int64 = -1
	private var stallEmitted = 0
	private var stallAsking = false
	private var stallAbsent = false
	private var stallTold = Date.distantPast
	private var stallFile: FileHandle?

	/// The file this run's stall record is also being written to, so a session can be handed
	/// over as one artefact rather than as a log query.
	private(set) var stallPath: URL?

	/// Off the sampling timer, so splitting and writing the record never sits between the
	/// engine and the next frame.
	private let stallQueue = DispatchQueue(label: "org.opents.shell.stalls", qos: .utility)

	func start() {
		lastFrames = 0
		lastSample = Date()
		runBegan = Date()
		drawn = false
		handler.reset()
		resetStallRecord()

		// Opened before the page is even asked for. The engine opens each image with a
		// probe it cannot overlap with anything, so on a network run those probes are the
		// first thing to cost minutes; done here they are one round trip taken while the
		// module is still compiling. A local set has nothing to open.
		DiscCache.shared.prime(DiscLibrary.shared.searchOrder)

		// Rebuilt per run rather than installed once: what they tell the page depends on
		// the discs, and those can be changed without quitting.
		let scripts = webView.configuration.userContentController
		scripts.removeAllUserScripts()
		for source in [Self.consoleBridge, Self.localDiscsBridge] where !source.isEmpty {
			scripts.addUserScript(WKUserScript(source: source,
			                                   injectionTime: .atDocumentStart, forMainFrameOnly: true))
		}

		webView.load(URLRequest(url: pageURL))

		poll?.invalidate()
		poll = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
			self?.sample()
		}
	}

	private func sample() {
		pollStalls()

		guard let onStatus else { return }

		// What the shell knows without asking the page, reported first. A read off a disc
		// is synchronous, so while one is outstanding the page cannot run a script at all
		// and nothing below would answer -- and that stretch is precisely the one a
		// progress readout exists for.
		var carried = Status()
		carried.frames = lastFrames
		carried.delivered = handler.delivered
		carried.failure = handler.failure
		onStatus(carried)

		state { [weak self] object in
			guard let self else { return }

			var status = Status()
			status.started = object["started"] as? Bool ?? false
			status.frames = object["frames"] as? Int ?? 0
			status.delivered = self.handler.delivered
			status.failure = self.handler.failure

			let now = Date()
			let elapsed = now.timeIntervalSince(self.lastSample)
			if elapsed > 0.25 && status.frames >= self.lastFrames {
				status.rate = Double(status.frames - self.lastFrames) / elapsed
			}
			if !self.drawn && status.frames > 0 {
				self.drawn = true
				let cache = DiscCache.shared
				self.log.info("""
					first frame after \(now.timeIntervalSince(self.runBegan), format: .fixed(precision: 2))s, \
					\(status.delivered) bytes read, \(cache.missed) reads off the network, \
					\(cache.joined) of them waiting on a request already out and \
					\(cache.hedged) of those asking again
					""")
			}

			self.lastFrames = status.frames
			self.lastSample = now

			// Every few seconds rather than every sample: enough to see a run's shape in a
			// log without being what the log is made of.
			self.sinceLog += 1
			if self.sinceLog >= 10 {
				self.sinceLog = 0
				self.log.debug("""
					frames \(status.frames) at \(status.rate, format: .fixed(precision: 1))/s, \
					read \(status.delivered) bytes, engine \(object, privacy: .public)
					""")
			}

			onStatus(status)
		}
	}

	/// Ends the run and starts a fresh one. A reload would do, but the engine keeps no
	/// state outside the page, so this is the honest name for what happens.
	func restart() {
		start()
	}

	// MARK: - The stall record

	/// Takes the engine's record of the reads it was blocked on and puts it where it can be
	/// read after the session: one line per stall in the unified log, and the same lines in
	/// a file the session can be handed over as.
	///
	/// Asked for on the sampling timer the status readout already runs on, and asked for
	/// only what has not been seen, so a mission's worth of stalls is carried a few lines at
	/// a time. Nothing here waits on the page: the request is asynchronous, one is in the
	/// air at a time, and what comes back is split and written on a queue of its own.
	///
	/// The engine is asked for the record two ways, because the shell must read whatever
	/// engine the bundle happens to hold. What the shell needs of it, in order of
	/// preference:
	///
	///   1. `window.OpenTS_State.stalls`, an array of strings, one line per stall, appended
	///      to as they happen, with `window.OpenTS_State.stallSeconds` the running total of
	///      seconds blocked. This wants nothing marshalled out of the module and is the
	///      shape the page already reports `frames` and `waits` in.
	///   2. `Module._OpenTS_Iso_Stalls(after)` returning a NUL terminated `char const *`
	///      holding the lines with a sequence number above `after`, oldest first, with
	///      `Module._OpenTS_Iso_Stall_Seconds()` the running total. Reading a string out of
	///      the module needs `UTF8ToString` or `HEAPU8` to be reachable from the page, and
	///      neither is exported by the build in `docs/BUILDING.md`; the script below falls
	///      back to walking the heap itself where it can.
	///
	/// A line is whatever the engine wrote and is logged unchanged, so the record survives
	/// a change of format. The one thing the shell reads out of it is the sequence number
	/// it must begin with, which is how a line already emitted is told from a new one and
	/// how a ring that dropped entries is noticed. What is wanted in the rest of it is:
	///
	///     <sequence> <monotonic seconds> <image> <offset> <length> <blocked ms> demand|ahead
	private func pollStalls() {
		guard !stallAbsent, !stallAsking else { return }
		stallAsking = true

		let script = """
		(function () {
			var page = window.OpenTS_State || {};
			if (Array.isArray(page.stalls)) {
				return JSON.stringify({ lines: page.stalls, total: page.stallSeconds || 0 });
			}

			if (typeof Module === "undefined" || !Module._OpenTS_Iso_Requests) { return ""; }
			if (!Module._OpenTS_Iso_Stalls) { return "absent"; }

			var at = Module._OpenTS_Iso_Stalls(\(stallSeq));
			var text = "";
			if (at) {
				if (typeof Module.UTF8ToString === "function") {
					text = Module.UTF8ToString(at);
				} else if (Module.HEAPU8) {
					var end = at;
					while (Module.HEAPU8[end]) { end++; }
					text = new TextDecoder().decode(Module.HEAPU8.subarray(at, end));
				} else {
					return "absent";
				}
			}

			var total = Module._OpenTS_Iso_Stall_Seconds ? Module._OpenTS_Iso_Stall_Seconds() : 0;
			return JSON.stringify({ lines: text.split("\\n"), total: total });
		}())
		"""

		webView.evaluateJavaScript(script) { [weak self] value, _ in
			guard let self else { return }

			// Empty is the engine not being up yet, which every run begins with.
			guard let text = value as? String, !text.isEmpty else {
				self.stallAsking = false
				return
			}

			guard text != "absent" else {
				self.stallAsking = false
				self.stallAbsent = true
				self.stalls.notice("""
					the engine in this bundle does not record the reads it was blocked on; \
					nothing will be logged for them
					""")
				return
			}

			guard let data = text.data(using: .utf8),
			      let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
			      let lines = object["lines"] as? [String] else {
				self.stallAsking = false
				return
			}

			// Everything the writing needs is taken here, on the thread that owns it, so
			// the queue below reads none of it and the next poll cannot start until what
			// this one found has been written down.
			let total = (object["total"] as? Double) ?? 0
			let after = self.stallSeq, already = self.stallEmitted
			let file = self.stallFile ?? self.openStallRecord(ifAny: lines)
			let due = Date().timeIntervalSince(self.stallTold) > 10

			self.stallQueue.async {
				let (seq, emitted) = self.write(lines, after: after, already: already,
				                                total: total, telling: due, to: file)
				DispatchQueue.main.async {
					self.stallSeq = seq
					self.stallEmitted = emitted
					if due { self.stallTold = Date() }
					self.stallAsking = false
				}
			}
		}
	}

	/// Writes out the stalls this poll had not seen before, and the running total often
	/// enough that a session that ends unexpectedly still leaves a headline number. Hands
	/// back how far the record has been read, for the poll after this one.
	private func write(_ lines: [String], after: Int64, already: Int, total: Double,
	                   telling: Bool, to file: FileHandle?) -> (Int64, Int) {
		var seq = after
		var emitted = already
		var index = 0
		var fresh: [String] = []

		for line in lines {
			let text = line.trimmingCharacters(in: .whitespacesAndNewlines)
			guard !text.isEmpty else { continue }
			index += 1

			// The engine's own sequence number where the line carries one, so a record
			// fetched whole is not emitted twice and a ring that dropped entries shows as a
			// gap rather than as a record that quietly lost them. Where it carries none,
			// position in the record is all there is to go on, which holds for as long as
			// the record is only ever appended to.
			if let head = text.split(separator: " ").first, let number = Int64(head) {
				guard number > seq else { continue }
				if seq >= 0 && number > seq + 1 {
					stalls.notice("""
						the engine dropped \(number - seq - 1) stalls before this one; its \
						record holds only the most recent
						""")
				}
				seq = number
			} else {
				guard index > emitted else { continue }
			}

			fresh.append(text)
			emitted += 1
		}

		for text in fresh { stalls.notice("stall \(text, privacy: .public)") }

		if !fresh.isEmpty, let file {
			try? file.write(contentsOf: Data((fresh.joined(separator: "\n") + "\n").utf8))
		}

		if telling {
			stalls.notice("\(emitted) stalls so far, \(total, format: .fixed(precision: 3))s blocked")
		}

		return (seq, emitted)
	}

	/// Forgets what the last run's record had reached, so this one is read from its start.
	private func resetStallRecord() {
		closeStallRecord()

		stallSeq = -1
		stallEmitted = 0
		stallAsking = false
		stallAbsent = false
		stallTold = Date.distantPast
		stallPath = nil
	}

	/// Starts a file for this run's stall record, once there is a stall to put in it. One
	/// file per run, named for when the run started, so a session is one artefact and a run
	/// with nothing to record leaves nothing behind.
	private func openStallRecord(ifAny lines: [String]) -> FileHandle? {
		guard lines.contains(where: { !$0.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty })
		else { return nil }

		let stamp = DateFormatter()
		stamp.dateFormat = "yyyyMMdd-HHmmss"

		guard let home = try? FileManager.default.url(for: .documentDirectory,
		                                              in: .userDomainMask,
		                                              appropriateFor: nil, create: true) else {
			return nil
		}

		let path = home.appendingPathComponent("stalls-\(stamp.string(from: runBegan)).txt")
		guard FileManager.default.createFile(atPath: path.path, contents: nil),
		      let file = try? FileHandle(forWritingTo: path) else { return nil }

		stallFile = file
		stallPath = path
		stalls.notice("this run's stall record is also being written to \(path.path, privacy: .public)")
		return file
	}

	/// Closes this run's stall record. Called when the app is going away, so a session that
	/// is quit rather than restarted still leaves the file complete.
	func closeStallRecord() {
		let file = stallFile
		stallFile = nil
		stallQueue.async { try? file?.close() }
	}

	/// What the page reports about the run, for a status readout or a support report.
	///
	/// The engine's own accounting is folded in where it is reachable, because the two
	/// answer different halves of the same question: the page knows whether frames are
	/// advancing, and the engine knows what it had to read to draw them.
	func state(_ done: @escaping ([String: Any]) -> Void) {
		let script = """
		(function () {
			var page = window.OpenTS_State || {};
			var state = {
				started: !!page.started, frames: page.frames || 0,
				waits: page.waits || 0, persistent: !!page.persistent
			};

			if (typeof Module !== "undefined" && Module._OpenTS_Iso_Requests) {
				state.localDiscs = !!Module.opentsLocalDiscs;
				state.isoRequests = Module._OpenTS_Iso_Requests();
				state.isoFetched = Module._OpenTS_Iso_Fetched();

				if (Module._OpenTS_Iso_Store_State) {
					state.storeState = Module._OpenTS_Iso_Store_State();
					state.storeBytes = Module._OpenTS_Iso_Store_Bytes();
					state.storeHits = Module._OpenTS_Iso_Store_Hits();
				}
			}

			return JSON.stringify(state);
		}())
		"""

		webView.evaluateJavaScript(script) { value, _ in
			guard let text = value as? String,
			      let data = text.data(using: .utf8),
			      let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
				return done([:])
			}
			done(object)
		}
	}

	// MARK: - Browser side storage

	/// What the engine keeps between runs, and which is the point of keeping it: the block
	/// store makes a second launch cheap, and a saved game is the only thing here a player
	/// cannot get back.
	struct Storage {
		var usage: UInt64 = 0
		var quota: UInt64 = 0
		/// Whether the engine's block store exists at all. With local discs it never does:
		/// the engine is told not to cache, because a read off this disk is already cheap.
		var blocks = false
		var saves = false

		/// Whether the page answered at all. It cannot before a run has loaded one, which is
		/// exactly when the first run panel is on screen, and what the shell keeps is known
		/// either way.
		var browser = false

		/// What the shell's own copy of the images is taking on this device. This is the
		/// larger of the two by far and the one a player would want back: the browser store
		/// is bounded to 64 MB by the engine, and this is bounded by the discs themselves.
		var discs: UInt64 = 0

		var anything: Bool { blocks || discs > 0 }
	}

	/// A page on this origin with nothing running on it, so storage can be read or emptied
	/// without an engine holding it open.
	private func idle(_ done: @escaping (Bool) -> Void) {
		poll?.invalidate()
		poll = nil

		var components = URLComponents()
		components.scheme = DiscSchemeHandler.scheme
		components.host = DiscSchemeHandler.host
		components.path = "/idle"

		idleDone = done
		webView.load(URLRequest(url: components.url!))
	}

	fileprivate var idleDone: ((Bool) -> Void)?

	/// What the two stores are holding.
	///
	/// The shell's own is measured here; the browser's is read out of the page, because
	/// WKWebsiteDataStore reports which origins hold data but not how much and the page's
	/// quota estimate does. Asking costs the run nothing, so it is asked of whatever page is
	/// loaded rather than stopping the game -- and when there is no page to ask, which is the
	/// first run panel's case, what the shell keeps is still reported.
	func storage(_ done: @escaping (Storage) -> Void) {
		var found = Storage()
		found.discs = DiscCache.shared.occupied()

		let script = """
		var estimate = { usage: 0, quota: 0 };
		if (navigator.storage && navigator.storage.estimate) {
			estimate = await navigator.storage.estimate();
		}

		var names = [];
		if (indexedDB.databases) {
			names = (await indexedDB.databases()).map(function (one) { return one.name; });
		}

		return {
			usage: estimate.usage || 0,
			quota: estimate.quota || 0,
			blocks: names.indexOf("opents-iso") >= 0,
			saves: names.indexOf("/save") >= 0
		};
		"""

		webView.callAsyncJavaScript(script, arguments: [:], in: nil, in: .page) { result in
			guard case .success(let value) = result, let object = value as? [String: Any] else {
				return done(found)
			}

			found.usage = UInt64((object["usage"] as? Double) ?? 0)
			found.quota = UInt64((object["quota"] as? Double) ?? 0)
			found.blocks = object["blocks"] as? Bool ?? false
			found.saves = object["saves"] as? Bool ?? false
			found.browser = true
			done(found)
		}
	}

	/// Empties both copies of the fetched blocks and nothing else: the images this shell
	/// keeps on disk, and the engine's own store in the browser.
	///
	/// The saved games are in browser storage too, on the same origin, so removing the
	/// origin's data -- which is the only thing the native website data store can do --
	/// would take them with it. The engine's block store is one named database, and deleting
	/// it by name leaves the saves where they are; the shell's images are its own files and
	/// are simply removed.
	func clearDiscCache(_ done: @escaping (String?) -> Void) {
		DiscCache.shared.empty()

		idle { ok in
			guard ok else { return done("the storage page could not be opened") }

			let script = """
			return await new Promise(function (resolve) {
				var request = indexedDB.deleteDatabase("opents-iso");
				request.onsuccess = function () { resolve(""); };
				request.onerror = function () { resolve("the browser refused: " + request.error); };
				request.onblocked = function () { resolve("something still has it open"); };
			});
			"""

			self.webView.callAsyncJavaScript(script, arguments: [:], in: nil, in: .page) { result in
				switch result {
				case .success(let value):
					let text = value as? String ?? ""
					done(text.isEmpty ? nil : text)
				case .failure(let error):
					done(error.localizedDescription)
				}
			}
		}
	}

	/// Tells the page that its transfer rate readout would be measuring a disk.
	///
	/// The page reads `Module.opentsLocalDiscs`, and it builds `Module` itself, in an
	/// inline script this cannot run before. So the property is claimed ahead of that
	/// assignment and the flag is set on whatever the page puts there. A mixed set counts
	/// as remote, because for those discs bytes really are coming down a wire.
	private static var localDiscsBridge: String {
		let local = DiscLibrary.shared.discs.allSatisfy { !$0.isRemote }
		guard local else { return "" }

		return """
		(function () {
			var held;
			Object.defineProperty(window, "Module", {
				configurable: true,
				get: function () { return held; },
				set: function (value) {
					if (value && typeof value === "object") { value.opentsLocalDiscs = true; }
					held = value;
				}
			});
		}());
		"""
	}

	private static let consoleBridge = """
	(function () {
		var forward = function (level) {
			return function () {
				try {
					window.webkit.messageHandlers.log.postMessage(
						level + ": " + Array.prototype.map.call(arguments, String).join(" "));
				} catch (error) {}
			};
		};
		var wrap = function (name) {
			var original = console[name];
			console[name] = function () {
				forward(name).apply(null, arguments);
				if (original) original.apply(console, arguments);
			};
		};
		wrap("log"); wrap("warn"); wrap("error");
	}());
	"""
}

extension GameSession: WKScriptMessageHandler {
	func userContentController(_ controller: WKUserContentController, didReceive message: WKScriptMessage) {
		log.debug("\(String(describing: message.body), privacy: .public)")
	}
}

extension GameSession: WKNavigationDelegate {
	func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
		settle(webView, true)
	}

	func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
		log.error("the page could not be loaded: \(error.localizedDescription, privacy: .public)")
		settle(webView, false)
	}

	func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
		log.error("the page could not be loaded: \(error.localizedDescription, privacy: .public)")
		settle(webView, false)
	}

	/// Releases whoever is waiting for the empty page, and only for that page: the game's own
	/// load finishes through here too, and answering it with the engine still on the page
	/// would empty a store that is being written.
	private func settle(_ webView: WKWebView, _ ok: Bool) {
		guard webView.url?.path == "/idle", let done = idleDone else { return }
		idleDone = nil
		done(ok)
	}
}

/// The webview the game is played in.
///
/// The game binds the right mouse button -- it is how a player deselects and how a unit is
/// ordered about -- so the platform's own use of that button has to be taken away, or the
/// first right click opens a menu over the battlefield instead.
final class GameWebView: WKWebView {
	#if os(macOS)
	override func willOpenMenu(_ menu: NSMenu, with event: NSEvent) {
		menu.removeAllItems()
	}

	override var acceptsFirstResponder: Bool { true }
	#else
	override func canPerformAction(_ action: Selector, withSender sender: Any?) -> Bool {
		false
	}
	#endif
}
