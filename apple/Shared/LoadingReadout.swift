/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// What the shells say while a run is still reading its working set. The Mac draws it with
// AppKit and the phone with UIKit, but both are reporting the same counters off the same
// session, so the wording and the arithmetic are here and each shell supplies only views.

import Foundation

enum LoadingReadout {
	/// The line beside the spinner, or `nil` once the engine has drawn and there is nothing
	/// left to say.
	static func line(for status: GameSession.Status, since: Date, now: Date = Date()) -> String? {
		if status.frames > 0 { return nil }

		guard status.delivered > 0 else { return "Starting the engine…" }

		let read = ByteCountFormatter.string(fromByteCount: Int64(status.delivered),
		                                     countStyle: .file)

		// The rate is averaged over the whole run rather than sampled: a block takes longer
		// than a sample does, so an instant rate is zero as often as it is anything. Under
		// two seconds there is not enough of a run to divide by.
		let elapsed = now.timeIntervalSince(since)
		guard elapsed >= 2 else { return "Reading the discs — \(read) so far" }

		let rate = ByteCountFormatter.string(fromByteCount: Int64(Double(status.delivered) / elapsed),
		                                     countStyle: .file)
		return "Reading the discs — \(read) at \(rate)/s"
	}

	/// The sentence under that line. A network run has a minutes long stretch with nothing
	/// on screen and no way for the player to tell it from a stall, so it says how long and
	/// that it happens once. A local set explains itself by being instant.
	static var explanation: String {
		DiscLibrary.shared.discs.contains(where: \.isRemote)
			? "This takes a few minutes the first time. After that it's quick."
			: ""
	}

	/// The heading on the one alert a disc that could not be read gets. What follows it is
	/// what the server answered, which the scheme handler supplies.
	static let failureTitle = "A disc image could not be read."
}
