/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Free space, on a target that may or may not have a disk. win32compat.h declares
// GetDiskFreeSpaceA; this is where it is answered, and it sits apart from win32compat.cpp
// because answering it means asking two unrelated hosts two different questions.
//
// The engine asks twice. Once at startup, where it refuses to run below eight megabytes,
// and once before the save dialog opens. Both are asking whether a saved game will fit.
//
// Under node the question is the ordinary one: -sNODERAWFS=1 hands the module the host's
// own filesystem, statvfs there is the host answering about itself, and it is passed
// straight through.
//
// A page has no filesystem to ask. Emscripten's in-memory one answers statvfs with a fixed
// four gigabytes total and two free -- figures its own source labels untrue -- whatever the
// page is running on, so repeating them would be inventing a number. What a page does have
// is its origin's storage quota, which the browser sizes against the device's real free
// space; navigator.storage.estimate reports it, and that is what is reported here.
//
// The quota is the closest real storage figure a page has, and it is not yet the figure
// that decides whether a save fits. A saved game is written into the in-memory filesystem,
// whose ceiling is the tab's JavaScript heap rather than the wasm heap and which no API
// reports, and which the tab discards when it closes. Nothing the engine writes is charged
// against the quota until saves are moved onto storage that outlives the tab, and that
// move is what makes this the exact bound rather than the nearest honest one.

#include "always.h"

#include "win32compat.h"

#if defined(__EMSCRIPTEN__)

#include "browser.h"

#include <emscripten/emscripten.h>

#include <sys/statvfs.h>


/*
 * Windows describes free space as a count of clusters, so a figure in bytes has to be
 * expressed in some geometry on the way out. This is an ordinary one.
 */
static DWORD const DISK_BYTES_PER_SECTOR = 512;
static DWORD const DISK_SECTORS_PER_CLUSTER = 8;

// The caller scales the cluster count into kilobytes and holds the result in 32 bits, which
// runs out at four terabytes. Anything larger is reported as the ceiling rather than as
// whatever the multiplication wrapped to.
static unsigned long long const DISK_MAX_CLUSTERS =
	0xFFFFFFFFull / ((DISK_BYTES_PER_SECTOR * DISK_SECTORS_PER_CLUSTER) / 1024);

// How long the page is given to answer before the request counts as unanswered. The promise
// settles in milliseconds when it settles at all.
static DWORD const DISK_ESTIMATE_TIMEOUT = 2000;

static bool _EstimateSampled = false;
static bool _EstimateValid = false;
static unsigned long long _EstimateFree = 0;
static unsigned long long _EstimateTotal = 0;


/// <summary>
/// Does the filesystem serving this path report figures of its own?
/// </summary>
/// <param name="path">The path whose filesystem is in question.</param>
/// <returns>bool; Would statvfs on this path be answered by the host rather than by
/// Emscripten's placeholder?</returns>
/// <remarks>A mount that knows its own size supplies a statfs operation and Emscripten asks
/// it; one that does not gets the placeholder. NODERAWFS is the case with no mount at all,
/// because it replaces the virtual filesystem with the host's rather than mounting it.</remarks>
static bool Filesystem_Reports_Its_Own_Space(char const * path)
{
	int answers = EM_ASM_INT({
		try {
			if (typeof FS !== "object" || FS === null) return 0;
			if (typeof FS.lookupPath !== "function") return 1;

			var node = FS.lookupPath(UTF8ToString($0), { follow: true }).node;
			return (node && node.node_ops && node.node_ops.statfs) ? 1 : 0;
		} catch (error) {
			return 0;
		}
	}, path);

	return(answers != 0);
}


/// <summary>
/// Asks the page how much storage its origin may still use, and waits for the answer.
/// </summary>
/// <returns>bool; Did the page answer?</returns>
/// <remarks>
/// The estimate is a promise, and the engine's caller is an ordinary synchronous Win32
/// entry point, so the wait is the engine's own yield -- the same wait the message box
/// stands on. Without the yield scaffold there is nothing to wait on and the question goes
/// unasked rather than being answered with a guess.
///
/// It is asked once. The quota moves with the device rather than with this page, nothing
/// the engine writes is charged against it, and asking again would cost an animation frame
/// to be told the same thing.
/// </remarks>
static bool Sample_Page_Storage(void)
{
	if (_EstimateSampled) {
		return(_EstimateValid);
	}

	_EstimateSampled = true;

	int offered = EM_ASM_INT({
		return (typeof navigator === "object" && navigator !== null && navigator.storage
			&& typeof navigator.storage.estimate === "function") ? 1 : 0;
	});

	if (offered == 0) {
		return(false);
	}

	if (!Browser_Yield_Is_Available()) {
		return(WIN32_UNSUPPORTED("GetDiskFreeSpace: waiting on the page's storage estimate without the yield scaffold", false));
	}

	/*
	 * Nothing below puts a comma anywhere but inside parentheses; the preprocessor splits
	 * the block on any other one, because brackets and braces do not group a macro argument.
	 */
	EM_ASM({
		globalThis.__opentsStorageEstimate = null;
		navigator.storage.estimate().then(function (estimate) {
			globalThis.__opentsStorageEstimate = Array(Number(estimate.quota) || 0, Number(estimate.usage) || 0);
		}).catch(function (error) {
			globalThis.__opentsStorageEstimate = Array(-1, -1);
		});
	});

	DWORD const start = timeGetTime();
	int state = 0;

	for (;;) {
		state = EM_ASM_INT({
			var estimate = globalThis.__opentsStorageEstimate;
			if (estimate === null || estimate === undefined) return 0;
			return (estimate[0] < 0) ? -1 : 1;
		});

		if (state != 0) break;
		if ((DWORD)(timeGetTime() - start) >= DISK_ESTIMATE_TIMEOUT) break;

		Browser_Yield();
	}

	if (state != 1) {
		return(false);
	}

	double const quota = EM_ASM_DOUBLE({ return globalThis.__opentsStorageEstimate[0]; });
	double const usage = EM_ASM_DOUBLE({ return globalThis.__opentsStorageEstimate[1]; });

	EM_ASM({ delete globalThis.__opentsStorageEstimate; });

	_EstimateTotal = (quota > 0.0) ? (unsigned long long)quota : 0;
	_EstimateFree = (quota > usage) ? (unsigned long long)(quota - usage) : 0;
	_EstimateValid = (_EstimateTotal > 0);

	return(_EstimateValid);
}


/// <summary>
/// Reports the geometry and the free and total cluster counts of the storage behind a path.
/// </summary>
/// <param name="root">The volume to report on. Null names the current directory, as Windows
/// has it, and is what the engine passes.</param>
/// <param name="sectorspercluster">Filled in with the sectors a cluster holds.</param>
/// <param name="bytespersector">Filled in with the bytes a sector holds.</param>
/// <param name="freeclusters">Filled in with the clusters still free.</param>
/// <param name="totalclusters">Filled in with the clusters the storage holds.</param>
/// <returns>BOOL; TRUE when the figures are real, FALSE when no host would give any.</returns>
BOOL GetDiskFreeSpaceA(LPCSTR root, LPDWORD sectorspercluster, LPDWORD bytespersector, LPDWORD freeclusters, LPDWORD totalclusters)
{
	char const * path = ((root != nullptr) && (root[0] != '\0')) ? root : ".";

	unsigned long long freebytes = 0;
	unsigned long long totalbytes = 0;
	bool answered = false;

	if (Filesystem_Reports_Its_Own_Space(path)) {
		struct statvfs space;

		if (statvfs(path, &space) == 0) {
			unsigned long long const unit = (space.f_frsize != 0) ? space.f_frsize : space.f_bsize;

			freebytes = (unsigned long long)space.f_bavail * unit;
			totalbytes = (unsigned long long)space.f_blocks * unit;
			answered = true;
		}
	}

	if (!answered && Sample_Page_Storage()) {
		freebytes = _EstimateFree;
		totalbytes = _EstimateTotal;
		answered = true;
	}

	if (!answered) {
		return(WIN32_UNSUPPORTED("GetDiskFreeSpace: no filesystem and no page able to report free space", FALSE));
	}

	unsigned long long const clusterbytes = (unsigned long long)DISK_BYTES_PER_SECTOR * DISK_SECTORS_PER_CLUSTER;

	unsigned long long freecount = freebytes / clusterbytes;
	unsigned long long totalcount = totalbytes / clusterbytes;

	if (totalcount < freecount) totalcount = freecount;
	if (freecount > DISK_MAX_CLUSTERS) freecount = DISK_MAX_CLUSTERS;
	if (totalcount > DISK_MAX_CLUSTERS) totalcount = DISK_MAX_CLUSTERS;

	if (sectorspercluster != nullptr) *sectorspercluster = DISK_SECTORS_PER_CLUSTER;
	if (bytespersector != nullptr) *bytespersector = DISK_BYTES_PER_SECTOR;
	if (freeclusters != nullptr) *freeclusters = (DWORD)freecount;
	if (totalclusters != nullptr) *totalclusters = (DWORD)totalcount;

	return(TRUE);
}


/*
** ---------------------------------------------------------------------------------------
** The drives.
** ---------------------------------------------------------------------------------------
*/


UINT GetDriveTypeA(LPCSTR) { return(WIN32_STUB(DRIVE_UNKNOWN)); }
DWORD GetLogicalDrives(void) { return(WIN32_STUB(0)); }
BOOL GetVolumeInformationA(LPCSTR, LPSTR, DWORD, LPDWORD, LPDWORD, LPDWORD, LPSTR, DWORD) { return(WIN32_STUB(FALSE)); }

#endif	// __EMSCRIPTEN__
