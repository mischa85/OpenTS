/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The Mac shell: one window, one menu, and a settings panel the discs are named in.

import AppKit
import WebKit

final class AppDelegate: NSObject, NSApplicationDelegate {
	private var window: NSWindow!
	private var settingsWindow: NSWindow?

	/// The first run panel, when it is what the window is holding.
	private var firstRun: SettingsViewController?

	/// Shown over the game until it draws. A cold start reads a working set off the discs
	/// before there is anything to see, and over a network that is minutes.
	private let loading = NSTextField(labelWithString: "")

	/// The first failure has been reported; the engine will keep asking and the tenth
	/// message says no more than the first.
	private var reported = false

	func applicationDidFinishLaunching(_ notification: Notification) {
		NSApp.mainMenu = buildMenu()

		window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1024, height: 768),
		                  styleMask: [.titled, .closable, .miniaturizable, .resizable],
		                  backing: .buffered, defer: false)
		window.title = "OpenTS"
		window.minSize = NSSize(width: 640, height: 480)
		window.setFrameAutosaveName("OpenTS")
		window.center()
		window.makeKeyAndOrderFront(nil)

		NSApp.activate(ignoringOtherApps: true)

		GameSession.shared.onStatus = { [weak self] status in self?.show(status) }

		if DiscLibrary.shared.isConfigured {
			showGame()
		} else {
			showFirstRun()
		}
	}

	func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

	// MARK: - What the window holds

	private func showGame() {
		firstRun = nil
		reported = false

		let container = NSView(frame: window.contentLayoutRect)
		let web = GameSession.shared.webView!
		web.frame = container.bounds
		web.autoresizingMask = [.width, .height]
		container.addSubview(web)

		loading.font = .monospacedSystemFont(ofSize: 12, weight: .regular)
		loading.textColor = NSColor(calibratedWhite: 0.62, alpha: 1)
		loading.stringValue = "Starting the engine…"
		loading.translatesAutoresizingMaskIntoConstraints = false
		container.addSubview(loading)
		NSLayoutConstraint.activate([
			loading.centerXAnchor.constraint(equalTo: container.centerXAnchor),
			loading.centerYAnchor.constraint(equalTo: container.centerYAnchor),
		])

		window.contentView = container
		window.makeFirstResponder(web)
		GameSession.shared.start()
	}

	private func showFirstRun() {
		let controller = SettingsViewController(mode: .firstRun) { [weak self] in self?.showGame() }
		firstRun = controller
		window.contentView = controller.view
		window.makeFirstResponder(controller.view)
	}

	/// The loading line, and the one alert a failed disc gets.
	private func show(_ status: GameSession.Status) {
		if let failure = status.failure, !reported {
			reported = true
			loading.isHidden = true

			let alert = NSAlert()
			alert.messageText = "A disc image could not be read."
			alert.informativeText = failure
			alert.addButton(withTitle: "Choose Disc Images…")
			alert.addButton(withTitle: "Continue")
			if alert.runModal() == .alertFirstButtonReturn { chooseDiscs() }
			return
		}

		guard !loading.isHidden else { return }

		if status.frames > 0 {
			loading.isHidden = true
			return
		}

		let read = ByteCountFormatter.string(fromByteCount: Int64(status.delivered),
		                                     countStyle: .file)
		loading.stringValue = status.delivered == 0
			? "Starting the engine…"
			: "Reading the discs — \(read) so far"
	}

	// MARK: - Menu

	/// Every top level entry has to be a submenu; an item placed directly in the menu bar
	/// is accepted and then silently never shown.
	private func buildMenu() -> NSMenu {
		let bar = NSMenu()
		let name = ProcessInfo.processInfo.processName

		let application = NSMenu()
		application.addItem(withTitle: "About \(name)", action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)), keyEquivalent: "")
		application.addItem(.separator())
		application.addItem(withTitle: "Settings…", action: #selector(showSettings), keyEquivalent: ",")
		application.addItem(.separator())
		let services = NSMenu()
		let servicesItem = application.addItem(withTitle: "Services", action: nil, keyEquivalent: "")
		servicesItem.submenu = services
		NSApp.servicesMenu = services
		application.addItem(.separator())
		application.addItem(withTitle: "Hide \(name)", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
		let hideOthers = application.addItem(withTitle: "Hide Others", action: #selector(NSApplication.hideOtherApplications(_:)), keyEquivalent: "h")
		hideOthers.keyEquivalentModifierMask = [.command, .option]
		application.addItem(withTitle: "Show All", action: #selector(NSApplication.unhideAllApplications(_:)), keyEquivalent: "")
		application.addItem(.separator())
		application.addItem(withTitle: "Quit \(name)", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")

		let game = NSMenu(title: "Game")
		game.addItem(withTitle: "Choose Disc Images…", action: #selector(chooseDiscs), keyEquivalent: "d")
		game.addItem(withTitle: "Restart Engine", action: #selector(restartEngine), keyEquivalent: "r")

		let view = NSMenu(title: "View")
		let fullScreen = view.addItem(withTitle: "Enter Full Screen", action: #selector(NSWindow.toggleFullScreen(_:)), keyEquivalent: "f")
		fullScreen.keyEquivalentModifierMask = [.command, .control]
		view.addItem(.separator())
		let hud = view.addItem(withTitle: "Show Diagnostics", action: #selector(toggleDiagnostics), keyEquivalent: "")
		hud.state = GameSession.shared.diagnostics ? .on : .off

		// Deliberately without a Close item. The window is the game and this target has no
		// way to save a mission, so Command-W in muscle memory would end one silently.
		let windowMenu = NSMenu(title: "Window")
		windowMenu.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
		windowMenu.addItem(withTitle: "Zoom", action: #selector(NSWindow.performZoom(_:)), keyEquivalent: "")
		windowMenu.addItem(.separator())
		windowMenu.addItem(withTitle: "Bring All to Front", action: #selector(NSApplication.arrangeInFront(_:)), keyEquivalent: "")

		let help = NSMenu(title: "Help")
		help.addItem(withTitle: "OpenTS on the Web", action: #selector(openProjectPage), keyEquivalent: "")

		for submenu in [application, game, view, windowMenu, help] {
			let item = NSMenuItem()
			item.submenu = submenu
			bar.addItem(item)
		}

		NSApp.windowsMenu = windowMenu
		NSApp.helpMenu = help
		return bar
	}

	// MARK: - Menu actions

	@objc private func showSettings() {
		if let firstRun {
			firstRun.refresh()
			return
		}

		if let settingsWindow {
			settingsWindow.makeKeyAndOrderFront(nil)
			(settingsWindow.contentViewController as? SettingsViewController)?.refresh()
			return
		}

		let controller = SettingsViewController(mode: .settings) { [weak self] in
			self?.settingsWindow?.close()
			self?.settingsWindow = nil
			self?.showGame()
		}

		let panel = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 620, height: 560),
		                     styleMask: [.titled, .closable], backing: .buffered, defer: false)
		panel.title = "OpenTS Settings"
		panel.contentViewController = controller
		panel.center()
		panel.makeKeyAndOrderFront(nil)
		settingsWindow = panel
	}

	@objc private func chooseDiscs() {
		if let firstRun {
			firstRun.chooseFiles()
			return
		}

		SettingsViewController.runOpenPanel(for: window) { [weak self] urls in
			guard !urls.isEmpty else { return }
			DiscLibrary.shared.setFiles(urls)
			self?.showGame()
		}
	}

	@objc private func restartEngine() {
		guard DiscLibrary.shared.isConfigured else { return }
		showGame()
	}

	@objc private func toggleDiagnostics(_ sender: NSMenuItem) {
		GameSession.shared.diagnostics.toggle()
		sender.state = GameSession.shared.diagnostics ? .on : .off
		if firstRun == nil { GameSession.shared.restart() }
	}

	@objc private func openProjectPage() {
		NSWorkspace.shared.open(URL(string: "https://github.com/OpenTS-Developers/OpenTS")!)
	}
}
