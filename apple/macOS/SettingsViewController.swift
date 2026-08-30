/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Where the discs are named and what the browser side storage is holding. It is one panel
// used twice: it fills the window before there is anything to play, and it is what the
// Settings item opens afterwards, so there is one place to change a disc rather than two.
//
// OpenTS supplies the engine and not the game data, so a fresh installation has nothing to
// run and this is the first thing it shows.

import AppKit
import UniformTypeIdentifiers

final class SettingsViewController: NSViewController {
	enum Mode {
		/// Filling the main window, with nothing to go back to.
		case firstRun
		/// A panel over a run that is already going.
		case settings
	}

	private let mode: Mode
	private let done: () -> Void

	private let discSummary = NSTextField(labelWithString: "")
	private let addressField = NSTextField()
	private let addressNote = NSTextField(labelWithString: "")
	private let storageSummary = NSTextField(wrappingLabelWithString: "")
	private let clearButton = NSButton()
	private let confirm = NSButton()

	init(mode: Mode, done: @escaping () -> Void) {
		self.mode = mode
		self.done = done
		super.init(nibName: nil, bundle: nil)
	}

	required init?(coder: NSCoder) { fatalError("not loaded from a nib") }

	override func loadView() {
		// Semantic colours throughout: this is native chrome, so it follows the user's
		// appearance and their Increase Contrast setting rather than a palette of its own.
		let root = NSView(frame: NSRect(x: 0, y: 0, width: 620, height: 560))
		root.wantsLayer = true
		root.layer?.backgroundColor = NSColor.windowBackgroundColor.cgColor

		let title = NSTextField(labelWithString: "Command & Conquer: Tiberian Sun")
		title.font = .systemFont(ofSize: 21, weight: .semibold)

		let blurb = Self.body(
			"OpenTS is the engine. The game data is not part of it, and this app ships with "
			+ "none. Point it at your own Tiberian Sun and Firestorm disc images and it will "
			+ "read them from this Mac. They are read where they are; nothing is copied or "
			+ "installed.")

		let choose = NSButton(title: "Choose Disc Images…", target: self, action: #selector(chooseFilesAction))
		choose.bezelStyle = .rounded
		choose.controlSize = .large
		if mode == .firstRun { choose.keyEquivalent = "\r" }

		discSummary.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
		discSummary.textColor = .secondaryLabelColor
		discSummary.maximumNumberOfLines = 6

		let advanced = Self.note(
			"Images can also be read from a server that answers ranged requests. Enter one "
			+ "or more addresses separated by spaces, in the order to search them. Nothing "
			+ "is supplied: this is for a server you have.")

		addressField.placeholderString = "https://example.org/FIRESTORM.iso"
		addressField.font = .monospacedSystemFont(ofSize: 11, weight: .regular)

		let useAddress = NSButton(title: "Use Addresses", target: self, action: #selector(useAddressAction))
		useAddress.bezelStyle = .rounded

		addressNote.font = .systemFont(ofSize: 11)
		addressNote.textColor = .systemRed
		addressNote.maximumNumberOfLines = 3

		let addressRow = NSStackView(views: [addressField, useAddress])
		addressRow.orientation = .horizontal
		addressRow.spacing = 8
		addressField.setContentHuggingPriority(.defaultLow, for: .horizontal)

		let storageTitle = NSTextField(labelWithString: "Storage")
		storageTitle.font = .systemFont(ofSize: 13, weight: .semibold)

		storageSummary.font = .systemFont(ofSize: 11)
		storageSummary.textColor = .secondaryLabelColor
		storageSummary.maximumNumberOfLines = 5
		storageSummary.preferredMaxLayoutWidth = 520

		clearButton.title = "Clear Cached Discs…"
		clearButton.bezelStyle = .rounded
		clearButton.target = self
		clearButton.action = #selector(clearAction)

		confirm.title = mode == .firstRun ? "Play" : "Done"
		confirm.bezelStyle = .rounded
		confirm.controlSize = .large
		confirm.target = self
		confirm.action = #selector(confirmAction)
		if mode == .settings { confirm.keyEquivalent = "\r" }

		let column = NSStackView(views: [
			title, blurb, choose, discSummary,
			NSBox.separator(), advanced, addressRow, addressNote,
			NSBox.separator(), storageTitle, storageSummary, clearButton,
			confirm,
		])
		column.orientation = .vertical
		column.alignment = .leading
		column.spacing = 12
		column.translatesAutoresizingMaskIntoConstraints = false
		column.setCustomSpacing(20, after: discSummary)
		column.setCustomSpacing(20, after: addressNote)
		column.setCustomSpacing(22, after: clearButton)

		root.addSubview(column)
		NSLayoutConstraint.activate([
			column.centerXAnchor.constraint(equalTo: root.centerXAnchor),
			column.centerYAnchor.constraint(equalTo: root.centerYAnchor),
			column.widthAnchor.constraint(equalToConstant: 520),
		])

		view = root
		refresh()
	}

	// MARK: - Actions

	@objc private func chooseFilesAction() { chooseFiles() }

	func chooseFiles() {
		Self.runOpenPanel(for: view.window) { [weak self] urls in
			guard !urls.isEmpty else { return }
			DiscLibrary.shared.setFiles(urls)

			guard let self else { return }
			self.addressField.stringValue = ""
			self.addressNote.stringValue = DiscLibrary.shared.isConfigured ? ""
				: "None of those files could be read as a disc image."
			self.refresh()
			if DiscLibrary.shared.isConfigured { self.done() }
		}
	}

	/// Reports what it rejected rather than quietly keeping the good half: a player who
	/// mistyped one of three addresses would otherwise be left with two discs and no reason.
	@objc private func useAddressAction() {
		let written = addressField.stringValue
			.split(whereSeparator: { $0.isWhitespace || $0 == "," })
			.map(String.init)

		guard !written.isEmpty else {
			addressNote.stringValue = "Enter an address first."
			return
		}

		var accepted: [URL] = []
		var rejected: [String] = []

		for text in written {
			guard let url = URL(string: text), let scheme = url.scheme?.lowercased(),
			      scheme == "http" || scheme == "https", url.host != nil else {
				rejected.append(text)
				continue
			}
			accepted.append(url)
		}

		guard rejected.isEmpty else {
			addressNote.stringValue = "Not a usable address: " + rejected.joined(separator: ", ")
				+ ". An address has to begin with http:// or https://."
			return
		}

		addressNote.stringValue = ""
		DiscLibrary.shared.setRemote(accepted)
		refresh()
		done()
	}

	@objc private func clearAction() {
		let alert = NSAlert()
		alert.messageText = "Clear what has been fetched from the discs?"
		alert.informativeText =
			"The next run fetches what it needs again, which over a network takes about a "
			+ "minute before the menu appears.\n\nSaved games are kept: they are stored "
			+ "separately and are not touched.\n\nThe current run ends."
		alert.addButton(withTitle: "Clear")
		alert.addButton(withTitle: "Cancel")

		guard alert.runModal() == .alertFirstButtonReturn else { return }

		clearButton.isEnabled = false
		storageSummary.stringValue = "Clearing…"

		GameSession.shared.clearDiscCache { [weak self] failure in
			guard let self else { return }
			self.clearButton.isEnabled = true

			if let failure {
				self.storageSummary.stringValue = "The cache could not be cleared: \(failure)"
			} else {
				self.refresh()
			}
		}
	}

	@objc private func confirmAction() { done() }

	// MARK: - Reading back

	func refresh() {
		let discs = DiscLibrary.shared.searchOrder
		confirm.isEnabled = !discs.isEmpty
		discSummary.stringValue = discs.isEmpty
			? "No discs chosen."
			: discs.map { "\($0.label)\($0.isRemote ? "  (server)" : "")" }.joined(separator: "\n")

		// Nothing has been read yet, so there is nothing to say about storage.
		guard !discs.isEmpty else {
			clearButton.isHidden = true
			storageSummary.stringValue = "Nothing is stored yet."
			return
		}

		// A local set is never cached, so there is nothing to report and nothing to clear.
		// Saying that is more use than a row of zeroes.
		if discs.allSatisfy({ !$0.isRemote }) {
			clearButton.isHidden = true
			storageSummary.stringValue =
				"Local disc images are read straight off this Mac, so nothing is cached and "
				+ "no storage is used for them. Saved games are still kept."
			return
		}

		clearButton.isHidden = false
		storageSummary.stringValue = "Reading…"

		GameSession.shared.storage { [weak self] storage in
			guard let self else { return }

			let bytes = { (count: UInt64) in
				ByteCountFormatter.string(fromByteCount: Int64(count), countStyle: .file)
			}

			var lines: [String] = []
			lines.append(storage.discs > 0
				? "What has been fetched from the discs is kept on this Mac: "
				+ "\(bytes(storage.discs)) of at most \(bytes(DiscCache.limit)), which is no "
				+ "more than the discs themselves. The system may reclaim it if the disk fills."
				: "Nothing has been fetched from the discs yet.")
			if storage.browser {
				lines.append("Browser storage holds a further \(bytes(storage.usage)) for this app.")
				if storage.saves { lines.append("Saved games are stored separately and are kept.") }
			}

			self.storageSummary.stringValue = lines.joined(separator: " ")
			self.clearButton.isEnabled = storage.anything
		}
	}

	// MARK: - The panel

	static func runOpenPanel(for window: NSWindow?, done: @escaping ([URL]) -> Void) {
		let panel = NSOpenPanel()
		panel.title = "Choose Disc Images"
		panel.message = "Select the Tiberian Sun and Firestorm disc images."
		panel.prompt = "Use These"
		panel.allowsMultipleSelection = true
		panel.canChooseDirectories = false
		panel.canChooseFiles = true

		// A disc image is data as far as the system is concerned; the extensions are named
		// so the common ones are offered rather than to refuse anything else.
		var types: [UTType] = [.data]
		for name in ["iso", "img", "bin"] {
			if let type = UTType(filenameExtension: name) { types.insert(type, at: 0) }
		}
		panel.allowedContentTypes = types

		let handle: (NSApplication.ModalResponse) -> Void = { response in
			done(response == .OK ? panel.urls : [])
		}

		if let window {
			panel.beginSheetModal(for: window, completionHandler: handle)
		} else {
			handle(panel.runModal())
		}
	}

	// MARK: - Labels

	private static func body(_ text: String) -> NSTextField {
		let field = NSTextField(wrappingLabelWithString: text)
		field.font = .systemFont(ofSize: 13)
		field.textColor = .secondaryLabelColor
		field.preferredMaxLayoutWidth = 520
		return field
	}

	private static func note(_ text: String) -> NSTextField {
		let field = NSTextField(wrappingLabelWithString: text)
		field.font = .systemFont(ofSize: 11)
		field.textColor = .secondaryLabelColor
		field.preferredMaxLayoutWidth = 520
		return field
	}
}

private extension NSBox {
	static func separator() -> NSBox {
		let box = NSBox()
		box.boxType = .separator
		return box
	}
}
