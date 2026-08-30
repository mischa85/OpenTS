/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Either the game or the screen that asks where the discs are. OpenTS supplies the engine
// and not the game data, so a fresh installation has nothing to run and says so.
//
// A disc can arrive two ways. The document picker reaches anything the Files app can see,
// including iCloud Drive and an external disk; and because the app declares file sharing,
// an image dropped into its own Documents folder from a Mac is found without a picker.

import UIKit
import UniformTypeIdentifiers

final class RootViewController: UIViewController {
	private var setup: UIStackView?
	private let summary = UILabel()
	private let storage = UILabel()
	private var clearButton = UIButton()

	override func viewDidLoad() {
		super.viewDidLoad()
		view.backgroundColor = .systemBackground

		if DiscLibrary.shared.isConfigured {
			showGame()
		} else {
			showSetup()
		}
	}

	override var prefersHomeIndicatorAutoHidden: Bool { setup == nil }
	override var prefersStatusBarHidden: Bool { setup == nil }

	// MARK: - The game

	private func showGame() {
		setup?.removeFromSuperview()
		setup = nil

		let web = GameSession.shared.webView!
		web.translatesAutoresizingMaskIntoConstraints = false
		view.addSubview(web)

		// Pinned to the safe area: a canvas run under the notch or the home indicator puts
		// part of the battlefield where it cannot be clicked.
		let guide = view.safeAreaLayoutGuide
		NSLayoutConstraint.activate([
			web.leadingAnchor.constraint(equalTo: guide.leadingAnchor),
			web.trailingAnchor.constraint(equalTo: guide.trailingAnchor),
			web.topAnchor.constraint(equalTo: guide.topAnchor),
			web.bottomAnchor.constraint(equalTo: guide.bottomAnchor),
		])

		setNeedsStatusBarAppearanceUpdate()
		setNeedsUpdateOfHomeIndicatorAutoHidden()
		GameSession.shared.start()
	}

	// MARK: - Setup

	private func showSetup() {
		let title = UILabel()
		title.text = "Command & Conquer: Tiberian Sun"
		title.font = .systemFont(ofSize: 22, weight: .semibold)
		title.numberOfLines = 0

		let blurb = UILabel()
		blurb.text = "OpenTS is the engine. The game data is not part of it, and this app "
			+ "ships with none. Point it at your own Tiberian Sun and Firestorm disc images "
			+ "and it will read them from this device."
		blurb.font = .systemFont(ofSize: 15)
		blurb.textColor = .secondaryLabel
		blurb.numberOfLines = 0

		var choose = UIButton.Configuration.filled()
		choose.title = "Choose Disc Images…"
		let chooseButton = UIButton(configuration: choose, primaryAction: UIAction { [weak self] _ in
			self?.pickFiles()
		})

		summary.font = .monospacedSystemFont(ofSize: 12, weight: .regular)
		summary.textColor = .secondaryLabel
		summary.numberOfLines = 0

		storage.font = .systemFont(ofSize: 12)
		storage.textColor = .secondaryLabel
		storage.numberOfLines = 0

		var clear = UIButton.Configuration.bordered()
		clear.title = "Clear Cached Discs…"
		clearButton = UIButton(configuration: clear, primaryAction: UIAction { [weak self] _ in
			self?.confirmClear()
		})

		var play = UIButton.Configuration.bordered()
		play.title = "Play"
		let playButton = UIButton(configuration: play, primaryAction: UIAction { [weak self] _ in
			guard DiscLibrary.shared.isConfigured else { return }
			self?.showGame()
		})

		let column = UIStackView(arrangedSubviews: [title, blurb, chooseButton, summary,
		                                            storage, clearButton, playButton])
		column.axis = .vertical
		column.alignment = .leading
		column.spacing = 16
		column.translatesAutoresizingMaskIntoConstraints = false
		view.addSubview(column)

		let guide = view.safeAreaLayoutGuide
		NSLayoutConstraint.activate([
			column.centerYAnchor.constraint(equalTo: guide.centerYAnchor),
			column.leadingAnchor.constraint(equalTo: guide.leadingAnchor, constant: 28),
			column.trailingAnchor.constraint(equalTo: guide.trailingAnchor, constant: -28),
		])

		setup = column
		adoptDroppedImages()
		refresh()
	}

	/// Anything already sitting in the app's own Documents folder, put there over file
	/// sharing. It is only adopted when nothing else is configured, so a player who chose
	/// their discs deliberately keeps that choice.
	private func adoptDroppedImages() {
		guard !DiscLibrary.shared.isConfigured else { return }

		let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
		let found = ((try? FileManager.default.contentsOfDirectory(at: documents,
		                                                          includingPropertiesForKeys: nil)) ?? [])
			.filter { ["iso", "img", "bin"].contains($0.pathExtension.lowercased()) }
			.sorted { $0.lastPathComponent < $1.lastPathComponent }

		guard !found.isEmpty else { return }
		DiscLibrary.shared.setFiles(found)
	}

	private func pickFiles() {
		var types: [UTType] = [.data]
		for name in ["iso", "img", "bin"] {
			if let type = UTType(filenameExtension: name) { types.insert(type, at: 0) }
		}

		let picker = UIDocumentPickerViewController(forOpeningContentTypes: types, asCopy: false)
		picker.allowsMultipleSelection = true
		picker.delegate = self
		present(picker, animated: true)
	}

	private func refresh() {
		let discs = DiscLibrary.shared.searchOrder
		summary.text = discs.isEmpty
			? "No discs chosen."
			: discs.map { "\($0.label)\($0.isRemote ? "  (server)" : "")" }.joined(separator: "\n")

		// A local set is never cached, so there is nothing to report and nothing to clear.
		if !discs.isEmpty && discs.allSatisfy({ !$0.isRemote }) {
			clearButton.isHidden = true
			storage.text = "Local disc images are read straight off this device, so nothing "
				+ "is cached. Saved games are still kept."
			return
		}

		clearButton.isHidden = false
		storage.text = "Reading storage…"

		GameSession.shared.storage { [weak self] found in
			guard let self else { return }

			let bytes = { (count: UInt64) in
				ByteCountFormatter.string(fromByteCount: Int64(count), countStyle: .file)
			}

			self.storage.text = (found.discs > 0
				? "What has been fetched from the discs is kept on this device and is "
				+ "holding \(bytes(found.discs)), out of a limit of \(bytes(DiscCache.limit))."
				: "Nothing has been fetched from the discs yet.")
				+ (found.browser ? " Browser storage holds a further \(bytes(found.usage))." : "")
			self.clearButton.isEnabled = found.anything
		}
	}

	/// Clearing ends the run and refetches on the next one, so it is confirmed first.
	private func confirmClear() {
		let alert = UIAlertController(
			title: "Clear what has been fetched from the discs?",
			message: "The next run fetches what it needs again. Saved games are kept.",
			preferredStyle: .alert)

		alert.addAction(UIAlertAction(title: "Cancel", style: .cancel))
		alert.addAction(UIAlertAction(title: "Clear", style: .destructive) { [weak self] _ in
			self?.storage.text = "Clearing…"
			GameSession.shared.clearDiscCache { failure in
				if let failure {
					self?.storage.text = "The cache could not be cleared: \(failure)"
				} else {
					self?.refresh()
				}
			}
		})

		present(alert, animated: true)
	}
}

extension RootViewController: UIDocumentPickerDelegate {
	func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
		guard !urls.isEmpty else { return }
		DiscLibrary.shared.setFiles(urls)
		refresh()
		showGame()
	}
}
