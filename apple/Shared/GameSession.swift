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

	func start() {
		lastFrames = 0
		lastSample = Date()

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
		guard let onStatus else { return }

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

	/// Read rather than measured natively, because WKWebsiteDataStore reports which origins
	/// hold data but not how much, and the page's own quota estimate does. Asking costs the
	/// run nothing, so it is asked of whatever page is loaded rather than stopping the game.
	func storage(_ done: @escaping (Storage?) -> Void) {
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
				return done(nil)
			}
			done(Storage(usage: UInt64((object["usage"] as? Double) ?? 0),
			             quota: UInt64((object["quota"] as? Double) ?? 0),
			             blocks: object["blocks"] as? Bool ?? false,
			             saves: object["saves"] as? Bool ?? false))
		}
	}

	/// Empties the block store and nothing else.
	///
	/// The saved games are in browser storage too, on the same origin, so removing the
	/// origin's data -- which is the only thing the native website data store can do --
	/// would take them with it. The block store is one named database, and deleting it by
	/// name leaves the saves where they are.
	func clearBlockStore(_ done: @escaping (String?) -> Void) {
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
	func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
		log.error("the page could not be loaded: \(error.localizedDescription, privacy: .public)")
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
