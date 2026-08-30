/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The entry point, written out rather than left to @main. The delegate is installed before
// the application runs, and the activation policy is stated: the player asked for a real
// window, which means a Dock tile, a menu bar of its own, and the standard title controls.

import AppKit

let application = NSApplication.shared
let delegate = AppDelegate()

application.delegate = delegate
application.setActivationPolicy(.regular)
application.run()
