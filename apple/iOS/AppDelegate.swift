/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The iPhone and iPad shell. One window, one root controller, and the same session the Mac
// runs; a phone has no menu bar, so what the Mac puts in a Game menu is reached here from
// the setup screen instead.

import UIKit

final class AppDelegate: UIResponder, UIApplicationDelegate {
	var window: UIWindow?

	func application(_ application: UIApplication,
	                 didFinishLaunchingWithOptions options: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
		let window = UIWindow(frame: UIScreen.main.bounds)
		window.rootViewController = RootViewController()
		window.makeKeyAndVisible()
		self.window = window

		application.isIdleTimerDisabled = true
		return true
	}

	/// What has arrived from a server but is not yet written down. Blocks are recorded only
	/// after their bytes are durably on this device, so without this an app the system takes
	/// away loses the last couple of seconds of fetching.
	func applicationDidEnterBackground(_ application: UIApplication) {
		DiscCache.shared.flush()
	}

	/// The stall record's lines are on disk as they are written, so this only closes the
	/// file; a session the system takes away without calling this still leaves it complete.
	func applicationWillTerminate(_ application: UIApplication) {
		DiscCache.shared.flush()
		GameSession.shared.closeStallRecord()
	}
}
