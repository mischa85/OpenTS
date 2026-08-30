#!/bin/sh
#
#                                O P E N  T S
#
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 OpenTS contributors
#
# See LICENSE.md for applicable additional terms and warranty disclaimers.
#
# Copies a WebAssembly build's page and modules into the app bundle. The artifacts are
# built by CMake and are not in the repository, so this runs at build time and fails
# loudly rather than producing an app with nothing to run.
#
# OPENTS_WEB_DIRS is a colon separated list of `bin` directories to take from. There is
# more than one because the two suspension builds are separate configurations: a JSPI
# build emits Game.js, an ASYNCIFY build emits Game-asyncify.js, and the page picks
# between them at load time. Naming only one is supported; the page then reports the
# absent module rather than a browser that cannot play.

set -eu

dirs="${OPENTS_WEB_DIRS:?OPENTS_WEB_DIRS is not set}"
destination="${BUILT_PRODUCTS_DIR:?}/${UNLOCALIZED_RESOURCES_FOLDER_PATH:?}/web"

rm -rf "$destination"
mkdir -p "$destination"

copied_page=0
copied_module=0

IFS=:
for dir in $dirs; do
	unset IFS
	[ -d "$dir" ] || continue

	if [ "$copied_page" -eq 0 ] && [ -f "$dir/index.html" ]; then
		cp "$dir/index.html" "$destination/"
		copied_page=1
	fi

	for module in Game.js Game.wasm Game-asyncify.js Game-asyncify.wasm; do
		if [ -f "$dir/$module" ] && [ ! -f "$destination/$module" ]; then
			cp "$dir/$module" "$destination/"
			copied_module=1
		fi
	done

	IFS=:
done
unset IFS

if [ "$copied_page" -eq 0 ] || [ "$copied_module" -eq 0 ]; then
	echo "error: no WebAssembly build found under OPENTS_WEB_DIRS ($dirs)." >&2
	echo "note: see apple/README.md for the two CMake configurations that produce them." >&2
	exit 1
fi

echo "note: bundled $(ls "$destination" | tr '\n' ' ')"
