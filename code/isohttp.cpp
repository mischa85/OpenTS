/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The browser half of the file layer's data path. isohttp.h explains what it is for; what
// this file has to solve is that ReadFile is synchronous and the web is not.
//
// The transport is a synchronous request, and it has to be. The alternative -- suspending
// the engine's own stack across a fetch on JavaScript Promise Integration -- would cost
// less and would not stall the page, but a suspend is only legal underneath a promising
// export, and the engine's first file open is not: MapSeedClass's constructor is a static
// object's, and it reaches Fetch_String, Init_Language_Resources and RawFileClass::
// Is_Available before main is entered. A fetch there throws SuspendError and ends the run.
// So the fetch transport can only return once the engine opens no file from a static
// constructor, and until then this is the transport that works everywhere the engine reads
// from.
//
// What that costs: the thread the page draws on is held for the length of a request, and a
// document is only allowed to answer a synchronous request as text, so its bytes arrive
// through a string at two bytes each and are copied out one at a time. A worker is allowed
// the array buffer and takes it, which is the other half of why moving the module onto one
// is worth doing.
//
// What a synchronous transport cannot do is overlap, and a movie is where that shows: it is
// read a frame at a time while it plays, so every read that leaves the block the last one
// ended in stops the decoding for a round trip and the frame is late. A read that continues
// a run is therefore also the moment the blocks in front of it are asked for, and that
// request is left in flight rather than waited on -- an ordinary fetch, which suspends
// nothing and is legal anywhere. It lands while the engine is decoding and handing the page
// the thread, and the read that wanted it copies it off the heap. Only a run that has proved
// itself is followed, and only a bounded distance in front of it, so a run given up costs a
// window and never a file.
//
// The asking happens before the read that opened the window is paid for, so a clip opened
// inside one frame -- which reads its blocks back to back, with no decoding in between --
// spends one round trip on two requests rather than one apiece.
//
// The third thing this file has to solve is that a reload should not pay for the image
// again. What is fetched is therefore kept in the browser's database -- the same store the
// saved games are on, so a page needs no worker and no cross-origin isolation to reach it --
// and a database is asked asynchronously. The wait that answers it is the engine's own
// suspension, and that is legal only underneath a promising export, which is why the store
// is opened at the first read that happens once main has been entered rather than when the
// image is. Reads before that point, and a build without the suspension scaffold, go to the
// network exactly as they did; the store is an optimization and never a requirement.

#include "isohttp.h"

#if defined(__EMSCRIPTEN__)

#include <emscripten/emscripten.h>
#include <vector>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>


/*
** The transport. Reports the number of bytes delivered, or a negative value for a request
** that failed or that came back without the range that was asked for.
*/
EM_JS(int, ISO_Http_Transfer, (char const * url, double offset, void * buffer, unsigned int length), {
	try {
		var request = new XMLHttpRequest();
		request.open("GET", UTF8ToString(url), false);
		request.setRequestHeader("Range", "bytes=" + offset + "-" + (offset + length - 1));

		var astext = false;
		try {
			request.responseType = "arraybuffer";
		} catch (error) {
			astext = true;
		}
		if (astext) request.overrideMimeType("text/plain; charset=x-user-defined");

		request.send(null);
		if (request.status !== 206) return -1;

		if (!astext) {
			var bytes = new Uint8Array(request.response);
			var count = Math.min(bytes.length, length);
			HEAPU8.set(bytes.subarray(0, count), buffer);
			return count;
		}

		var text = request.responseText;
		var written = Math.min(text.length, length);
		for (var index = 0; index < written; index++) {
			HEAPU8[buffer + index] = text.charCodeAt(index) & 255;
		}
		return written;
	} catch (error) {
		return -1;
	}
});


/*
** Which of a location's two forms identifies the image, worked out without asking anybody.
**
** The location the page was given, made absolute, and not the URL a request ends at. A
** large image is commonly served from a pool that redirects each request to whichever node
** answers it, so the URL a run ends at is not a property of the image at all and keying on
** it would give a second run a slot the first one wrote nothing under. What the run has
** already fetched would then be re-fetched, once per node.
**
** Resolving it is arithmetic over the document's own address, so it costs nothing and is
** available before anything has been asked of the server. A host with no document -- node,
** or a test -- resolves nothing and the location stands for itself.
*/
EM_JS(void, ISO_Http_Identity, (char const * url, char * identity, int size), {
	var text = UTF8ToString(url);

	try {
		text = new URL(text, location.href).href;
	} catch (error) {}

	var count = 0;
	while (count < text.length && count + 1 < size) {
		var code = text.charCodeAt(count);
		HEAPU8[identity + count] = (code > 126 || code < 32) ? 63 : code;
		count++;
	}
	HEAPU8[identity + count] = 0;
});


/*
** The one request that establishes what is being read. It answers the image's length and
** whatever the server calls this version of it, which together with the location above are
** what identify the image to a later run.
**
** The validator carries what a redirect cannot: a node serving a different file answers
** with a different entity tag or date, and the length differs besides, so the image is
** identified by what it is rather than by which node handed it over.
*/
EM_JS(double, ISO_Http_Probe, (char const * url, char * validator, int validatorsize), {
	HEAPU8[validator] = 0;

	try {
		var request = new XMLHttpRequest();
		request.open("GET", UTF8ToString(url), false);
		request.setRequestHeader("Range", "bytes=0-0");
		request.send(null);
		if (request.status !== 206) return -1;

		var range = request.getResponseHeader("Content-Range");
		if (!range) return -1;

		var total = range.split("/")[1];
		if (!total || total === "*") return -1;

		var tag = request.getResponseHeader("ETag") || request.getResponseHeader("Last-Modified") || "";
		var count = 0;

		while (count < tag.length && count + 1 < validatorsize) {
			var code = tag.charCodeAt(count);
			HEAPU8[validator + count] = (code > 126 || code < 32) ? 63 : code;
			count++;
		}
		HEAPU8[validator + count] = 0;

		return parseFloat(total);
	} catch (error) {
		return -1;
	}
});


/*
**	------------------------------------------------------------------------------------
**	What a probe learned, kept where the next launch can read it.
**
**	It goes in ordinary local storage rather than in the database the blocks are in,
**	because it has to be readable before the engine has a wait to spend: an image is
**	opened from the first file the host cannot answer for, which the engine asks about
**	while its static objects are still being constructed, and only a synchronous store can
**	answer there. It is a line of text per image, so what it costs that store is nothing.
**
**	A browser that will not hand one over -- a private window, storage turned off, a host
**	with no such thing at all -- reports having nothing, and the run probes exactly as it
**	did before.
**	------------------------------------------------------------------------------------
*/

EM_JS(int, ISO_Probe_Recall, (char const * key, char * record, int size), {
	HEAPU8[record] = 0;

	try {
		var text = localStorage.getItem(UTF8ToString(key));
		if (typeof text !== "string" || text.length === 0) return 0;

		var count = 0;
		while (count < text.length && count + 1 < size) {
			var code = text.charCodeAt(count);
			HEAPU8[record + count] = (code > 126 || code < 32) ? 63 : code;
			count++;
		}
		HEAPU8[record + count] = 0;
		return 1;
	} catch (error) {
		return 0;
	}
});


EM_JS(void, ISO_Probe_Remember, (char const * key, char const * record), {
	try {
		localStorage.setItem(UTF8ToString(key), UTF8ToString(record));
	} catch (error) {}
});


EM_JS(void, ISO_Probe_Forget, (char const * key), {
	try {
		localStorage.removeItem(UTF8ToString(key));
	} catch (error) {}
});


/*
** Asks, once the run is under way, whether the image a record described is still the image
** the server has.
**
** It is the whole of what is given up by not probing, put back where it costs nothing: an
** ordinary fetch, started from a timer well after the engine has its data, waited on by
** nobody. What it can do about a mismatch is drop the record, so that the next launch
** probes; the blocks are then found under a signature they do not answer to and are
** cleared by the machinery that already does that. This run carries on with what it has,
** because the alternative is to throw away a store the game is reading out of on the
** strength of one late answer.
**
** Headers a caller cannot read are not evidence of anything. A cross-origin answer with no
** Content-Range to read is left alone rather than treated as a changed image.
*/
EM_JS(void, ISO_Probe_Watch, (char const * key, char const * url, double length, char const * validator, double delay), {
	var state = globalThis.__opentsIsoProbe;

	if (state === undefined) {
		state = {watched: {}};
		globalThis.__opentsIsoProbe = state;
	}

	var slot = UTF8ToString(key);
	if (state.watched[slot]) return;
	state.watched[slot] = true;

	var where = UTF8ToString(url);
	var named = UTF8ToString(validator);
	var total = length;

	var timer = setTimeout(function () {
		try {
			fetch(where, {headers: {"Range": "bytes=0-0"}, cache: "no-store"}).then(function (answer) {
				if (answer.status !== 206) return;

				var range = answer.headers.get("Content-Range");
				if (!range) return;

				var said = range.split("/")[1];
				if (!said || said === "*") return;

				var tag = answer.headers.get("ETag") || answer.headers.get("Last-Modified") || "";
				if (parseFloat(said) === total && tag === named) return;

				try { localStorage.removeItem(slot); } catch (error) {}

				state.stale = true;
				if (globalThis.OpenTS_State) globalThis.OpenTS_State.isoStale = true;
				console.warn("OpenTS: " + where + " is no longer the image its cache was " +
					"built from; the next run will read it again.");
			}).catch(function () {});
		} catch (error) {}
	}, delay);

	// A page is kept open by its own document; a host whose timers hold the process alive
	// would be held open for the delay by a check nothing is waiting for.
	if (timer && typeof timer.unref === "function") timer.unref();
});


/*
** Where the images are. The page is asked first so that a harness can point the module at
** other discs without rebuilding it, and the environment second so that the same module
** answers to a shell running it under node.
**
** A game is spread over several discs, so what is named is a list: an array of locations,
** or one string holding them separated by commas. A string naming a single image is the
** one-element case of that and keeps meaning what it did. The list comes back separated by
** newlines, which no location may contain.
*/
EM_JS(int, ISO_Image_Location_Query, (char * buffer, int size), {
	var named;

	if (typeof Module !== "undefined" && Module["opentsImage"]) {
		named = Module["opentsImage"];
	} else if (typeof process !== "undefined" && process.env && process.env["OPENTS_IMAGE"]) {
		named = process.env["OPENTS_IMAGE"];
	} else if (typeof document !== "undefined") {
		named = "opents-data.iso";
	} else {
		named = [];
	}

	var listed = Array.isArray(named) ? named : ("" + named).split(",");
	var wanted = [];

	listed.forEach(function (one) {
		var location = ("" + one).trim();
		if (location.length > 0) wanted.push(location);
	});

	var text = wanted.join("\n");
	var count = 0;

	while (count < text.length && count + 1 < size) {
		var code = text.charCodeAt(count);
		HEAPU8[buffer + count] = (code > 127) ? 63 : code;
		count++;
	}
	HEAPU8[buffer + count] = 0;
	return count;
});


/*
**	------------------------------------------------------------------------------------
**	The persisted store. Everything below waits on the browser's database, so all of it is
**	behind the suspension scaffold: without it a page has no wait to spend and the store
**	cannot be reached at all.
**	------------------------------------------------------------------------------------
*/
#if defined(OPENTS_WASM_JSPI)

/*
** Whether a wait is legal yet. A suspension is only allowed underneath the promising export,
** which is main, so the answer is whether main has been entered -- and the only place that
** can be seen from is the call that enters it. The hook is put in from the first read, which
** happens during static construction, long before the page starts the engine.
**
** A runtime that starts the engine itself rather than through Module.callMain is not hooked
** and answers no, which costs the store and nothing else.
*/
EM_JS(int, ISO_Store_Under_Main, (void), {
	var state = globalThis.__opentsIsoStore;

	if (state === undefined) {
		state = Object.create(null);
		state.main = false;
		state.db = null;
		state.usable = false;
		state.given = false;

		// Whether the origin has refused a write. Reads carry on: what is already there is
		// worth serving whether or not another block will fit beside it.
		state.full = false;

		// What the origin says it may hold, once something has asked. Negative until then.
		state.room = -1;

		// One batch of staged blocks per image, since several are read at once and each
		// writes its own batch.
		state.staged = new Map();
		globalThis.__opentsIsoStore = state;

		var hook = function () {
			if (typeof Module !== "object" || Module === null) return false;

			var start = Module["callMain"];
			if (typeof start !== "function" || start.__opentsIso) return false;

			var wrapped = function () {
				globalThis.__opentsIsoStore.main = true;
				return start.apply(this, arguments);
			};
			wrapped.__opentsIso = true;
			Module["callMain"] = wrapped;
			return true;
		};

		try {
			if (!hook() && typeof Module === "object" && Module !== null) {
				var ready = Module["onRuntimeInitialized"];
				Module["onRuntimeInitialized"] = function () {
					hook();
					if (typeof ready === "function") return ready.apply(this, arguments);
				};
			}
		} catch (error) {}
	}

	return state.main ? 1 : 0;
});


/*
** Opens the database and hands back the record describing what it holds for one image. A
** database that cannot be opened -- a private window, storage the user has turned off -- is
** not an error the run has to care about, so it is reported as simply having nothing.
**
** The database is opened once and shared; only the record is per image. Version 1 keyed the
** blocks by number alone, which no more than one image can answer to, and nothing in that
** layout says which image a block came from, so the upgrade throws it away.
*/
EM_ASYNC_JS(int, ISO_Store_Open, (char const * slot, char * record, int size), {
	var state = globalThis.__opentsIsoStore;
	if (state === undefined || state === null || state.given) return 0;

	try {
		if (state.db === null) {
			state.db = await new Promise(function (resolve, reject) {
				var request = indexedDB.open("opents-iso", 2);
				request.onupgradeneeded = function (event) {
					var db = request.result;
					if (!db.objectStoreNames.contains("blocks")) db.createObjectStore("blocks");
					if (!db.objectStoreNames.contains("meta")) db.createObjectStore("meta");

					if (event.oldVersion === 1) {
						request.transaction.objectStore("blocks").clear();
						request.transaction.objectStore("meta").clear();
					}
				};
				request.onsuccess = function () { resolve(request.result); };
				request.onerror = function () { reject(request.error); };
				request.onblocked = function () { reject(new Error("blocked")); };
			});
		}

		var key = UTF8ToString(slot);

		var text = await new Promise(function (resolve, reject) {
			var transaction = state.db.transaction("meta", "readonly");
			var get = transaction.objectStore("meta").get(key);
			get.onsuccess = function () { resolve(get.result || ""); };
			transaction.onerror = function () { reject(transaction.error); };
			transaction.onabort = function () { reject(transaction.error); };
		});

		/*
		** The record is written and read a byte at a time, so it is held to plain ASCII. The
		** line ends are its own structure and are the one control character it carries.
		*/
		var count = 0;
		while (count < text.length && count + 1 < size) {
			var code = text.charCodeAt(count);
			var plain = (code === 10) || (code >= 32 && code <= 126);
			HEAPU8[record + count] = plain ? code : 63;
			count++;
		}
		HEAPU8[record + count] = 0;

		state.usable = true;
		return 1;
	} catch (error) {
		state.db = null;
		state.usable = false;
		return 0;
	}
});


/*
** What the origin says it is allowed to store, in bytes, or zero when it will not say.
**
** It is the allowance rather than what is left of it. The allowance is a property of the
** machine and holds still; what is left of it moves as the store fills, and sizing the store
** from a figure the store itself moves is how a cache ends up chasing its own tail. What
** each image may take is a share of this, so several discs and whatever else the page keeps
** all fit inside it -- see ISOBlockIndexClass::STORE_SHARE.
**
** A browser that has no estimate to give answers nothing at all, and the image falls back to
** the fixed ceiling it used before any of this was asked.
*/
EM_ASYNC_JS(double, ISO_Store_Room, (void), {
	var state = globalThis.__opentsIsoStore;
	if (state === undefined || state === null) return 0;
	if (state.room >= 0) return state.room;

	state.room = 0;

	try {
		if (navigator.storage && typeof navigator.storage.estimate === "function") {
			var estimate = await navigator.storage.estimate();

			if (estimate && typeof estimate.quota === "number" && estimate.quota > 0) {
				state.room = estimate.quota;
			}
		}
	} catch (error) {}

	return state.room;
});


/*
** Reads a span out of the store. The whole span is done in one transaction, so an extent
** the game reads in a single call costs one wait rather than one per block, and a block the
** current batch has staged but not yet written is served from the batch.
**
** A block is held under its image's key and its number, so the same block number on another
** disc is another entry rather than the same one.
*/
EM_ASYNC_JS(int, ISO_Store_Read, (char const * slot, double offset, void * buffer, unsigned int length, unsigned int blocksize), {
	var state = globalThis.__opentsIsoStore;
	if (state === undefined || state === null || !state.usable || state.db === null) return 0;

	try {
		var key = UTF8ToString(slot);
		var batch = state.staged.get(key);
		var first = Math.floor(offset / blocksize);
		var last = Math.floor((offset + length - 1) / blocksize);
		var wanted = [];
		var index = 0;

		for (index = first; index <= last; index++) {
			if (!batch || !batch.has(index)) wanted.push(index);
		}

		var found = new Map();

		if (wanted.length > 0) {
			await new Promise(function (resolve, reject) {
				var transaction = state.db.transaction("blocks", "readonly");
				var blocks = transaction.objectStore("blocks");
				wanted.forEach(function (number) {
					var get = blocks.get(key + "|" + number);
					get.onsuccess = function () { if (get.result) found.set(number, get.result); };
				});
				transaction.oncomplete = function () { resolve(0); };
				transaction.onerror = function () { reject(transaction.error); };
				transaction.onabort = function () { reject(transaction.error); };
			});
		}

		var written = 0;

		for (index = first; index <= last; index++) {
			var data = (batch && batch.has(index)) ? batch.get(index) : found.get(index);
			if (!data) return 0;

			var at = index * blocksize;
			var from = Math.max(offset, at);
			var to = Math.min(offset + length, at + data.length);
			if (to <= from) return 0;

			HEAPU8.set(data.subarray(from - at, to - at), buffer + (from - offset));
			written += to - from;
		}

		return (written === length) ? 1 : 0;
	} catch (error) {
		return 0;
	}
});


/*
** Holds a block until the batch is written. The copy is taken here because the heap it came
** from moves when it grows, and because the block is the engine's buffer, which the engine
** goes on using.
*/
EM_JS(int, ISO_Store_Stage, (char const * slot, double index, void const * buffer, unsigned int length), {
	var state = globalThis.__opentsIsoStore;
	if (state === undefined || state === null || !state.usable || state.db === null) return 0;

	try {
		var key = UTF8ToString(slot);
		var batch = state.staged.get(key);

		if (!batch) {
			batch = new Map();
			state.staged.set(key, batch);
		}

		batch.set(index, HEAPU8.slice(buffer, buffer + length));
		return 1;
	} catch (error) {
		return 0;
	}
});


/*
** Writes the batch, the evictions it forced and the record that describes the result, all in
** the one transaction that makes them agree.
**
** Running out of quota is an ordinary outcome here and is answered by writing no more. The
** quota belongs to the origin rather than to any one image, so the refusal is remembered for
** all of them; what none of them gives up is reading, because the blocks an earlier batch
** managed to write are still there and are still worth every round trip they save.
*/
EM_ASYNC_JS(int, ISO_Store_Write, (char const * slot, char const * record, char const * removals), {
	var state = globalThis.__opentsIsoStore;
	if (state === undefined || state === null || !state.usable || state.db === null) return 0;
	if (state.full) {
		state.staged.delete(UTF8ToString(slot));
		return -1;
	}

	var key = UTF8ToString(slot);
	var text = UTF8ToString(record);
	var drop = UTF8ToString(removals);

	try {
		await new Promise(function (resolve, reject) {
			var transaction = state.db.transaction(Array("blocks", "meta"), "readwrite");
			var blocks = transaction.objectStore("blocks");
			var batch = state.staged.get(key);

			/*
			** Only this image's blocks are ever dropped. The others are described by records
			** of their own and are none of this one's business.
			*/
			if (drop === "*") {
				blocks.delete(IDBKeyRange.bound(key + "|", key + "|" + String.fromCharCode(65535)));
			} else if (drop.length > 0) {
				drop.split(",").forEach(function (number) {
					var index = parseFloat(number);
					if (!batch || !batch.has(index)) blocks.delete(key + "|" + index);
				});
			}

			if (batch) batch.forEach(function (data, index) { blocks.put(data, key + "|" + index); });
			transaction.objectStore("meta").put(text, key);

			transaction.oncomplete = function () { resolve(0); };
			transaction.onerror = function () { reject(transaction.error); };
			transaction.onabort = function () { reject(transaction.error); };
		});

		state.staged.delete(key);
		return 1;
	} catch (error) {
		state.staged.delete(key);

		if ((error && error.name) === "QuotaExceededError") {
			state.full = true;
			return -1;
		}

		/*
		** Anything else is a database that has stopped answering, and there is no reading it
		** either. That one is given up for every image and for the rest of the run.
		*/
		state.usable = false;
		state.given = true;
		return 0;
	}
});

// Lets go of a batch that will not be written, which is the only thing teardown may do.
EM_JS(void, ISO_Store_Forget, (char const * slot), {
	var state = globalThis.__opentsIsoStore;
	if (state !== undefined && state !== null && state.staged) state.staged.delete(UTF8ToString(slot));
});


/*
** Is a block cache worth keeping at all? A host reading the discs off local storage says so,
** and for one of those the cache is a second copy of something already at hand: it costs the
** browser's storage quota and saves nothing.
*/
EM_JS(int, ISO_Store_Wanted, (void), {
	return (typeof Module !== "undefined" && Module.opentsLocalDiscs) ? 0 : 1;
});


/*
**	------------------------------------------------------------------------------------
**	The look-ahead pool. A span is one request left in flight, and then the bytes it
**	delivered. Starting one is not a wait and never suspends, so the engine goes straight
**	back to what it was doing; the answer arrives while the page has the thread, which is
**	the whole of what makes a round trip something the decoding overlaps.
**	------------------------------------------------------------------------------------
*/

/*
** Builds the pool, which is where a request left in flight lives until something reads it.
** Everything below assumes it is there, so it is made before any of them are reached.
*/
EM_JS(void, ISO_Http_Ahead_Ready, (unsigned int blocksize), {
	if (globalThis.__opentsIsoAhead !== undefined) return;

	var pool = {
		spans: new Map(),

		/*
		** What one image has been told it will probably want, and when it was last read
		** from. Nothing speculative goes out while the reading is going on, and what is
		** already on its way is abandoned the moment it resumes.
		*/
		plans: new Map(),

		active: 0,
		timer: null,
		waste: 0,

		// How long the reading has to have stopped, how much a speculative request asks
		// for, and how much of it may be held unread, in milliseconds and bytes.
		HOLD: 12 * 1024 * 1024,

		// How many guesses may be outstanding at once, over every image. A browser gives a
		// page about six connections to one origin, and the reading has to be able to take
		// one the moment it wants it: the transport a read blocks on is synchronous, so a
		// read that finds them all busy is the engine stopped. Two leaves four for the
		// reading -- the read itself and the window being refilled in front of it -- and is
		// still two round trips of guessing overlapped, which is where its value is.
		GUESSES: 2,

		// The unit everything here is fetched, believed and stored in. Only a whole one of
		// these may be stored or served, so it is what a part-delivered span is cut back to.
		BLOCK: blocksize,

		find: function (key, offset, length) {
			var list = this.spans.get(key);
			if (!list) return null;

			for (var index = 0; index < list.length; index++) {
				var span = list[index];
				if (span.start <= offset && offset + length <= span.stop) return span;
			}
			return null;
		},

		// Whether a span is holding the bytes a read is asking for. A span is answered from
		// as its bytes arrive, so this asks what has arrived rather than whether the request
		// has finished.
		ready: function (span, offset, length) {
			return span.data !== null && offset >= span.start &&
				offset + length <= span.start + span.filled;
		},

		/*
		** Hands a span's bytes to whatever is waiting for them, which is done as they
		** arrive rather than at the end. A span that has finished wakes everything still
		** waiting on it, since nothing more is coming and a wait that is never answered
		** would hold the engine for the rest of the run.
		*/
		wake: function (span) {
			var edge = span.start + span.filled;
			var keep = [];

			for (var index = 0; index < span.waiters.length; index++) {
				if (span.done || span.waiters[index].need <= edge) {
					span.waiters[index].resolve(0);
				} else {
					keep.push(span.waiters[index]);
				}
			}

			span.waiters = keep;
		},

		forget: function (key, span) {
			var list = this.spans.get(key);
			if (list) {
				var at = list.indexOf(span);
				if (at >= 0) list.splice(at, 1);
			}
		},

		/*
		** Lets a span go and answers what of it was received and never read. Bytes that
		** never arrived are not waste: they are bandwidth correctly not spent, and counting
		** them would say the guessing cost what it saved.
		*/
		drop: function (key, span) {
			this.forget(key, span);

			try { span.control.abort(); } catch (error) {}

			span.done = true;
			this.wake(span);

			var unread = span.filled - span.used;
			return (unread > 0) ? unread : 0;
		},

		/*
		** Abandons a span that is still arriving, keeping the whole blocks of it that got
		** here. What has crossed the wire is paid for whether the run still wants it or not,
		** so it stays as a guess of its own until something reads it or the store takes it;
		** only the part block at the end is lost, since a part block may never be believed.
		*/
		release: function (key, span) {
			try { span.control.abort(); } catch (error) {}

			span.done = true;

			var edge = span.start + span.filled;
			var whole = Math.floor(edge / this.BLOCK) * this.BLOCK;

			if (whole > span.start + span.used) {
				var waste = edge - whole;

				span.stop = whole;
				span.filled = whole - span.start;
				span.ok = true;
				span.idle = true;
				this.wake(span);
				return (waste > 0) ? waste : 0;
			}

			this.wake(span);
			return this.drop(key, span);
		},

		copy: function (key, span, offset, buffer, length) {
			if (!this.ready(span, offset, length)) {
				if (span.done) this.waste += this.drop(key, span);
				return 0;
			}

			var at = offset - span.start;
			HEAPU8.set(span.data.subarray(at, at + length), buffer);

			// Sequential reads only ever move forward, so the high water mark is what
			// the span has given up and what is left of it is waste if it is dropped.
			if (at + length > span.used) span.used = at + length;
			if (span.used >= span.stop - span.start) this.drop(key, span);
			return 1;
		},

		/*
		** Says a run wants the connection back, which stops any further guessing at once.
		** What is already in flight is left to finish rather than abandoned: its round
		** trip is paid for whether it lands or not, it is on a connection of its own and
		** is never waited on, so nothing is bought by throwing it away and the bytes it
		** brings back go into the store. What a run must never queue behind is a request
		** that has not been made yet, and that is what stopping here prevents.
		*/
		yield: function (key) {
			this.active = (typeof performance === "object") ? performance.now() : Date.now();
		},

		/*
		** Asks for the next pieces of what the engine said it would want.
		**
		** It does not wait for the reading to stop, and this is the whole of what makes it
		** worth having. A guess needs a round trip to land, and on a distant link the gap
		** between two reads is shorter than one, so a guess that may only be made while
		** nothing is being read is never made at all -- which is exactly what a run against
		** a two-second link measures. What the reading is owed is that it never queues
		** behind a guess, and it does not: a guess is one more connection of the six a page
		** is allowed, is never waited on, and its bytes are banked in the store whether
		** anything reads them or not.
		**
		** What is held down instead is how much of it there can be at once, so the guessing
		** takes a share of the link rather than the whole of it. The share is counted over
		** every image rather than image by image: a set is several discs, all of them are
		** told what they will want, and a per-image count lets them come to more requests
		** than the six a page is allowed -- at which point a read does queue behind a guess,
		** which is the one thing the guessing is not permitted to cost.
		*/
		flying: function () {
			var count = 0;

			this.spans.forEach(function (list) {
				for (var index = 0; index < list.length; index++) {
					if (list[index].idle && !list[index].done) count++;
				}
			});

			return count;
		},

		drain: function () {
			var pool = this;
			var flying = pool.flying();

			this.plans.forEach(function (plan, key) {
				if (plan.ranges.length === 0) return;

				var list = pool.spans.get(key) || [];
				var held = 0;

				for (var index = 0; index < list.length; index++) {
					if (!list[index].idle || !list[index].done) continue;
					held += list[index].filled - list[index].used;
				}

				if (held >= pool.HOLD) return;

				while (flying < pool.GUESSES && plan.ranges.length > 0) {
					var range = plan.ranges[0];
					var take = Math.min(plan.span, range.stop - range.at);

					if (pool.open(key, range.at, range.at + take, true) === 0) break;

					plan.spent += take;
					plan.asked++;
					range.at += take;
					if (range.at >= range.stop) plan.ranges.shift();
					flying++;
				}
			});
		},

		/*
		** Puts one request in flight. The document cache is asked to stay out of it:
		** every read of an image names the same URL, and a cache entry takes one writer
		** at a time, so a span left in flight would hold the entry and the synchronous
		** transport would queue behind it -- which turns the read the window was
		** supposed to get in front of into two round trips instead of one. Nothing is
		** lost by refusing the cache; what is worth keeping is kept in the store.
		**
		** The body is read as it arrives rather than waited for. A span is answered from
		** the moment the bytes a read wants are in it, so asking for a megabyte does not
		** postpone the frame that needed the first block of it -- which is what makes
		** reaching a long way in front of a movie an improvement rather than a longer
		** wait for the first frame. It is also what makes abandoning a span cheap: what
		** has crossed the wire is already here and only the part block at the end is lost.
		*/
		open: function (key, start, stop, idle) {
			var list = this.spans.get(key);
			if (!list) {
				list = [];
				this.spans.set(key, list);
			}

			var pool = this;
			var total = stop - start;

			var span = {
				start: start, stop: stop, data: null, filled: 0, done: false, ok: false,
				used: 0, idle: idle, waiters: [], control: new AbortController()
			};

			var take = function (chunk) {
				var room = (span.stop - span.start) - span.filled;
				var count = (chunk.length < room) ? chunk.length : room;

				if (count > 0) {
					span.data.set(chunk.subarray(0, count), span.filled);
					span.filled += count;
					pool.wake(span);
				}
			};

			span.promise = fetch(key, {
					cache: "no-store",
					headers: { "Range": "bytes=" + start + "-" + (stop - 1) },
					signal: span.control.signal
				})
				.then(function (response) {
					// Checked before a byte of the body is touched: a server that answers
					// the whole file rather than the range would otherwise be read as one.
					if (response.status !== 206) throw new Error("range refused");

					span.data = new Uint8Array(total);

					if (!response.body || typeof response.body.getReader !== "function") {
						return response.arrayBuffer().then(function (delivered) {
							take(new Uint8Array(delivered));
						});
					}

					var reader = response.body.getReader();
					var pump = function () {
						return reader.read().then(function (piece) {
							if (piece.done) return;
							take(piece.value);
							return pump();
						});
					};

					return pump();
				})
				.then(function () {
					span.ok = (span.filled === (span.stop - span.start));
					span.done = true;
					pool.wake(span);
				})
				.catch(function () {
					span.done = true;
					pool.wake(span);
				});

			list.push(span);
			return total;
		}
	};

	globalThis.__opentsIsoAhead = pool;
});


/*
** Asks for a span without waiting for it. Reports the bytes asked for, a negative number for
** a span that is already on its way, and nothing at all for a request that was declined --
** one image with as much in flight as it is allowed, or a runtime with no fetch to make one
** with -- which is the answer that leaves the window to ask again.
*/
EM_JS(double, ISO_Http_Ahead_Start, (char const * url, double offset, double length, int flights), {
	var pool = globalThis.__opentsIsoAhead;

	if (pool === undefined) return 0;
	if (typeof fetch !== "function" || typeof AbortController !== "function") return 0;

	var key = UTF8ToString(url);
	var start = offset;
	var stop = offset + length;

	if (!(stop > start)) return 0;

	pool.active = (typeof performance === "object") ? performance.now() : Date.now();

	var list = pool.spans.get(key);
	if (!list) {
		list = [];
		pool.spans.set(key, list);
	}

	// Nothing already asked for is asked for again, and the number outstanding is held
	// down so a page never has more of the image in flight than the window allows. What
	// was asked for on a guess is not what the window is allowed, so it is not counted.
	var outstanding = 0;

	for (var index = 0; index < list.length; index++) {
		if (list[index].start < stop && list[index].stop > start) return -1;
		if (!list[index].idle && !list[index].done) outstanding++;
	}
	if (outstanding >= flights) return 0;

	return pool.open(key, start, stop, false);
});


/*
** Says the image is being read, and hands back the bytes that cost. A read the engine is
** waiting on, or a window being refilled in front of one, wants the connection rather than
** merely having it, so no further guess is made until it has had it; a read the pool answers
** by itself costs nothing and only says the reading has not stopped.
*/
EM_JS(double, ISO_Http_Ahead_Busy, (char const * url, int give), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	pool.active = (typeof performance === "object") ? performance.now() : Date.now();

	if (give === 0) return 0;

	var before = pool.waste;

	pool.yield(UTF8ToString(url));
	return pool.waste - before;
});


/*
** Queues a run of the image the engine says it will probably want before long. Nothing is
** fetched here: when it is fetched is the drainer's decision, and the drainer only ever
** decides yes while the image is not being read at all.
**
** The queue is short and takes the runs in the order they were named, so the choice a menu
** offers first is the one that lands first. A run already covered by one queued is not
** queued again, since the same file gets named more than once.
*/
EM_JS(double, ISO_Http_Idle_Add, (char const * url, double offset, double length, double span, int depth), {
	var pool = globalThis.__opentsIsoAhead;

	if (pool === undefined) return 0;
	if (typeof fetch !== "function" || typeof setInterval !== "function") return 0;
	if (!(length > 0) || !(span > 0)) return 0;

	var key = UTF8ToString(url);
	var plan = pool.plans.get(key);

	if (!plan) {
		plan = { ranges: [], span: span, spent: 0, asked: 0 };
		pool.plans.set(key, plan);
	}

	plan.span = span;

	for (var index = 0; index < plan.ranges.length; index++) {
		if (plan.ranges[index].at <= offset && offset + length <= plan.ranges[index].stop) return 0;
	}

	if (plan.ranges.length >= depth) return 0;

	plan.ranges.push({ at: offset, stop: offset + length });

	if (pool.timer === null) {
		pool.timer = setInterval(function () { pool.drain(); }, 250);
	}

	return length;
});


/*
** Hands over a run of whole blocks that a completed guess is holding and nothing has read,
** so it can be put in the store rather than dropped when the span goes. What is handed over
** is marked as taken, which is what lets the span be let go and what keeps it out of the
** figure for bytes nobody wanted -- it is in the store, which is where the next run reads it
** from. Only whole blocks are handed over, since only a whole block is fit to be stored.
*/
EM_JS(double, ISO_Http_Idle_Take, (char const * url, void * buffer, unsigned int max, unsigned int blocksize, double * at), {
	var pool = globalThis.__opentsIsoAhead;

	HEAPF64[at >> 3] = 0;
	if (pool === undefined) return 0;

	var key = UTF8ToString(url);
	var list = pool.spans.get(key);

	if (!list) return 0;

	for (var index = 0; index < list.length; index++) {
		var span = list[index];

		if (!span.idle || span.data === null) continue;

		var from = span.start + span.used;
		var first = Math.ceil(from / blocksize) * blocksize;
		var edge = Math.floor((span.start + span.filled) / blocksize) * blocksize;
		var count = Math.min(max, edge - first);

		count -= count % blocksize;
		if (count <= 0) continue;

		HEAPU8.set(span.data.subarray(first - span.start, first - span.start + count), buffer);
		HEAPF64[at >> 3] = first;

		span.used = (first + count) - span.start;
		if (span.done && span.used >= span.stop - span.start) pool.forget(key, span);
		return count;
	}

	return 0;
});


/*
** Gives up everything one image was told it would want. The queue goes and so do the spans
** that arrived on it and nothing read, which are bytes that have no home left: this is
** called when the store stops taking them, and a guess that cannot be banked is one the run
** has no way of ever being paid back for. Holding them would keep the pool at its limit for
** the rest of the run, which is what stops it fetching anything a run does want.
*/
EM_JS(double, ISO_Http_Idle_Cancel, (char const * url), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var key = UTF8ToString(url);

	pool.plans.delete(key);

	var list = pool.spans.get(key);
	if (!list) return 0;

	var wasted = 0;

	for (var index = list.length - 1; index >= 0; index--) {
		if (!list[index].idle) continue;
		wasted += pool.drop(key, list[index]);
	}

	return wasted;
});


/*
** How many bytes the drainer has asked for since the last time it was asked, so what a
** guess costs is counted where every other request to an image is counted. The drainer runs
** on a timer of its own and cannot reach the counting itself.
*/
EM_JS(double, ISO_Http_Idle_Spent, (char const * url, unsigned int * asked), {
	var pool = globalThis.__opentsIsoAhead;

	HEAPU32[asked >> 2] = 0;
	if (pool === undefined) return 0;

	/*
	** Carried on from here as well as from the timer. A page holds no timer at all while a
	** synchronous request is in progress, and the loading is very nearly all synchronous
	** requests, so the reads are the only thing that runs often enough to keep a queue
	** moving while the engine is loading.
	*/
	pool.drain();

	var plan = pool.plans.get(UTF8ToString(url));
	if (!plan) return 0;

	var spent = plan.spent;

	HEAPU32[asked >> 2] = plan.asked;
	plan.spent = 0;
	plan.asked = 0;
	return spent;
});


/*
** What the pool can do for a span the engine is about to read: 0 nothing, 1 a request whose
** bytes have not got here yet, 2 the bytes themselves. A span still arriving answers 2 as
** soon as the part being asked for is in it, whatever is left of the request. One that
** finished without delivering them is let go here, so the read falls back to the transport
** rather than asking about it again.
*/
EM_JS(int, ISO_Http_Ahead_State, (char const * url, double offset, unsigned int length), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var key = UTF8ToString(url);
	var span = pool.find(key, offset, length);

	if (span === null) return 0;
	if (pool.ready(span, offset, length)) return 2;
	if (!span.done) return 1;

	pool.waste += pool.drop(key, span);
	return 0;
});


/*
** Takes bytes the pool is already holding. This is the case worth having: it costs a copy
** and nothing else, no request and no wait, which is what a read that the look-ahead got
** in front of should cost.
*/
EM_JS(int, ISO_Http_Ahead_Copy, (char const * url, double offset, void * buffer, unsigned int length), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var key = UTF8ToString(url);
	var span = pool.find(key, offset, length);

	if (span === null || !span.done) return 0;
	return pool.copy(key, span, offset, buffer, length);
});


/*
** Waits for the part of a look-ahead span a read needs. The wait ends when those bytes have
** arrived rather than when the whole request has, so a read never waits on bytes it is not
** asking for. It is the engine's own suspension and is legal only underneath the promising
** export, which is why the caller asks whether main has been entered first.
*/
EM_ASYNC_JS(int, ISO_Http_Ahead_Wait, (char const * url, double offset, void * buffer, unsigned int length), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var key = UTF8ToString(url);
	var span = pool.find(key, offset, length);

	if (span === null) return 0;

	if (!pool.ready(span, offset, length) && !span.done) {
		await new Promise(function (resolve) {
			span.waiters.push({ need: offset + length, resolve: resolve });
		});
	}

	return pool.copy(key, span, offset, buffer, length);
});


/*
** How much the pool is holding that no read has taken. A span kept back rather than thrown
** away has not been read yet and may never be, so it is speculation the connection has
** already paid for and belongs in the same figure as the spans that were let go: what is
** reported is bandwidth spent on bytes nobody wanted, whether or not the spending is over.
**
** What was asked for and never arrived is not in it. A request abandoned part way costs the
** bytes that crossed the wire and no more, and counting the rest would report the guessing
** as having cost what abandoning it saved.
*/
EM_JS(double, ISO_Http_Ahead_Unread, (void), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var unread = 0;

	pool.spans.forEach(function (list) {
		for (var index = 0; index < list.length; index++) {
			var held = list[index].filled - list[index].used;

			if (held > 0) unread += held;
		}
	});

	return unread;
});


/*
** Lets go of everything outstanding for one image and answers the bytes of it that were
** never read, which is what the look-ahead cost the connection and gave nothing back for.
*/
EM_JS(double, ISO_Http_Ahead_Drop, (char const * url), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var key = UTF8ToString(url);
	var list = pool.spans.get(key);

	if (!list) return 0;

	var waste = 0;
	var index = list.length;

	while (index-- > 0) {
		if (index < list.length) waste += pool.release(key, list[index]);
	}

	return waste;
});


/*
** Lets go of what was asked for in front of one run that has been displaced, leaving the
** spans belonging to the runs still being read along where they are.
*/
EM_JS(double, ISO_Http_Ahead_Drop_Range, (char const * url, double start, double stop), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var key = UTF8ToString(url);
	var list = pool.spans.get(key);

	if (!list) return 0;

	var waste = 0;

	for (var index = list.length - 1; index >= 0; index--) {
		var span = list[index];
		if (span.start < stop && span.stop > start) waste += pool.release(key, span);
	}

	return waste;
});

#endif	// OPENTS_WASM_JSPI


static char const ISO_STORE_MAGIC[] = "opents-iso-1";


ISOBlockIndexClass::ISOBlockIndexClass(void) :
	Total(0),
	Ceiling(STORE_LIMIT)
{
}


/// <summary>Builds the key that says which image a stored block belongs to.</summary>
/// <remarks>
/// The length alone would let an image of the same size be read as this one, and a run that
/// serves one image's sectors as another's corrupts game data in a way that looks like
/// anything but a cache. So whatever the server will say about the version -- an entity tag,
/// a modification date -- is part of the key, and a server that says nothing leaves the key
/// resting on the location and the length, which is the weakest form this takes: a run then
/// believes stored blocks whenever the same location still answers with the same number of
/// bytes. That is the assumption a browser's own cache makes of such a server, and the
/// location is the same one either way, so nothing here is believed that it would not be.
/// </remarks>
std::string ISOBlockIndexClass::Signature(char const * location, std::uint64_t length, char const * validator)
{
	if (location == nullptr || *location == '\0' || length == 0) return(std::string());

	std::string key(location);

	key += '|';

	char count[32];
	std::snprintf(count, sizeof(count), "%llu", (unsigned long long)length);
	key += count;

	key += '|';
	if (validator != nullptr) key += validator;

	/*
	**	The key is a line of the stored record, so anything that would end that line, and
	**	anything a byte-at-a-time copy through the heap would not survive, is flattened.
	*/
	for (char & character : key) {
		if (character < 0x20 || character > 0x7E) character = '?';
	}

	if (key.size() > SIGNATURE_MAX) key.resize(SIGNATURE_MAX);
	return(key);
}


/// <summary>Builds the key the store holds one image's blocks and record under.</summary>
/// <remarks>
/// The location alone, so that a new version of an image is written over the old one rather
/// than left beside it with nothing left to find it by. What decides whether those blocks
/// may still be served is the signature inside the record kept under this key. Two images
/// named separately keep separate slots, since they are separate locations.
/// </remarks>
std::string ISOBlockIndexClass::Store_Slot(char const * location)
{
	if (location == nullptr || *location == '\0') return(std::string());

	std::string slot(location);

	for (char & character : slot) {
		if (character < 0x20 || character > 0x7E) character = '?';
	}

	if (slot.size() > SIGNATURE_MAX) slot.resize(SIGNATURE_MAX);
	return(slot);
}


void ISOBlockIndexClass::Reset(std::string const & signature)
{
	Sig = signature;
	Total = 0;
	Order.clear();
	Held.clear();
}


/// <summary>Takes on a stored record, if it was written for this image.</summary>
bool ISOBlockIndexClass::Adopt(char const * record, std::string const & signature)
{
	Reset(signature);

	if (record == nullptr || signature.empty()) return(false);

	std::string const text(record);

	std::size_t const magic = text.find('\n');
	if (magic == std::string::npos) return(false);
	if (text.compare(0, magic, ISO_STORE_MAGIC) != 0) return(false);

	std::size_t const key = text.find('\n', magic + 1);
	if (key == std::string::npos) return(false);
	if (text.compare(magic + 1, key - magic - 1, signature) != 0) return(false);

	std::size_t cursor = key + 1;

	while (cursor < text.size()) {
		std::size_t stop = text.find(',', cursor);
		if (stop == std::string::npos) stop = text.size();

		std::size_t const colon = text.find(':', cursor);
		if (colon == std::string::npos || colon >= stop) {
			Reset(signature);
			return(false);
		}

		char * end = nullptr;
		unsigned long long const index = std::strtoull(text.c_str() + cursor, &end, 10);
		if (end != text.c_str() + colon) {
			Reset(signature);
			return(false);
		}

		unsigned long long const size = std::strtoull(text.c_str() + colon + 1, &end, 10);
		if (end != text.c_str() + stop || size == 0) {
			Reset(signature);
			return(false);
		}

		if (Held.insert((std::uint64_t)index).second) {
			EntryType entry;
			entry.Index = (std::uint64_t)index;
			entry.Size = (std::uint64_t)size;
			Order.push_back(entry);
			Total += (std::uint64_t)size;
		}

		cursor = stop + 1;
	}

	return(true);
}


std::string ISOBlockIndexClass::Encode(void) const
{
	std::string text(ISO_STORE_MAGIC);

	text += '\n';
	text += Sig;
	text += '\n';

	char entry[48];

	for (std::size_t position = 0; position < Order.size(); position++) {
		std::snprintf(entry, sizeof(entry), "%s%llu:%llu", (position != 0) ? "," : "",
			(unsigned long long)Order[position].Index, (unsigned long long)Order[position].Size);
		text += entry;
	}

	return(text);
}


/// <summary>Records a block as stored, evicting whatever no longer fits.</summary>
/// <remarks>
/// Eviction is by age of arrival. What a mission reads it reads again next time, so the set
/// stored is the set wanted and the order barely matters; what does matter is that the
/// eviction is decided here rather than by the database, so the record and the blocks it
/// describes are written together and cannot disagree.
///
/// A block nobody has read displaces nothing. The guessing offers far more than the reading
/// does and offers it in one long pass, so letting it evict would leave a full store holding
/// whichever part of a disc the pass finished on rather than the part the game turned out to
/// read. Declining costs the block and nothing else: it was fetched either way.
/// </remarks>
void ISOBlockIndexClass::Note(std::uint64_t index, std::uint64_t size,
	std::vector<std::uint64_t> & evicted, AdmitType how)
{
	if (size == 0 || Held.count(index) != 0) return;
	if (size > Ceiling) return;

	if (how == ADMIT_GUESS) {
		if (Total + size > Ceiling) return;
	} else {
		while (!Order.empty() && Total + size > Ceiling) {
			EntryType const oldest = Order.front();

			Order.erase(Order.begin());
			Held.erase(oldest.Index);
			Total -= oldest.Size;
			evicted.push_back(oldest.Index);
		}

		if (Total + size > Ceiling) return;
	}

	EntryType entry;
	entry.Index = index;
	entry.Size = size;
	Order.push_back(entry);
	Held.insert(index);
	Total += size;
}


/// <summary>Stops serving blocks a write turned out not to have stored.</summary>
void ISOBlockIndexClass::Forget(std::vector<std::uint64_t> const & indices)
{
	for (std::uint64_t index : indices) {
		if (Held.erase(index) == 0) continue;

		for (std::size_t position = 0; position < Order.size(); position++) {
			if (Order[position].Index != index) continue;

			Total -= Order[position].Size;
			Order.erase(Order.begin() + (std::ptrdiff_t)position);
			break;
		}
	}
}


/// <summary>Sets how much of this image may be kept.</summary>
void ISOBlockIndexClass::Cap(std::uint64_t bytes, std::vector<std::uint64_t> & evicted)
{
	if (bytes == 0) return;

	Ceiling = bytes;

	while (!Order.empty() && Total > Ceiling) {
		EntryType const oldest = Order.front();

		Order.erase(Order.begin());
		Held.erase(oldest.Index);
		Total -= oldest.Size;
		evicted.push_back(oldest.Index);
	}
}


/*
**	What the stored record opens with, so that a record this build cannot read is told from
**	one that is simply not there. Anything else under the key is left alone and the image is
**	probed, which is what an older or a newer build's record gets.
*/
static char const * const ISO_PROBE_MAGIC = "opents-probe-1";


ISOProbeClass::ISOProbeClass(void) :
	Length(0),
	Trip(0.0),
	Rate(0.0)
{
}


/// <summary>Takes on a stored record.</summary>
/// <remarks>
/// The validator comes last and takes the rest of the line, since it is the one field
/// whose contents are the server's to choose and a separator inside it would otherwise
/// have to be escaped. Everything before it is a number the record wrote itself.
/// </remarks>
bool ISOProbeClass::Decode(char const * text)
{
	Length = 0;
	Validator.clear();
	Trip = 0.0;
	Rate = 0.0;

	if (text == nullptr) return(false);

	std::string const line(text);
	std::size_t cursor = 0;
	std::string field[4];

	for (std::string & part : field) {
		std::size_t const stop = line.find('|', cursor);
		if (stop == std::string::npos) return(false);

		part = line.substr(cursor, stop - cursor);
		cursor = stop + 1;
	}

	if (field[0] != ISO_PROBE_MAGIC) return(false);

	char const * const count = field[1].c_str();
	char * end = nullptr;
	double const length = std::strtod(count, &end);

	if (end == count || *end != '\0' || !(length > 0.0)) return(false);

	Length = (std::uint64_t)length;
	Trip = std::strtod(field[2].c_str(), nullptr);
	Rate = std::strtod(field[3].c_str(), nullptr);
	Validator = line.substr(cursor);

	if (!(Trip > 0.0)) Trip = 0.0;
	if (!(Rate > 0.0)) Rate = 0.0;

	return(true);
}


std::string ISOProbeClass::Encode(void) const
{
	char head[128];

	std::snprintf(head, sizeof(head), "%s|%llu|%.3f|%.3f|", ISO_PROBE_MAGIC,
		(unsigned long long)Length, Trip, Rate);

	std::string record(head);

	record += Validator;

	/*
	**	The record is written and read a byte at a time, so it is held to plain printable
	**	ASCII the same way the key and the signature are.
	*/
	for (char & character : record) {
		if (character < 0x20 || character > 0x7E) character = '?';
	}

	if (record.size() > RECORD_MAX - 1) record.resize(RECORD_MAX - 1);
	return(record);
}


/*
**	Where the browser keeps what a probe learned about one image, which is a store of its
**	own rather than the database the blocks are in; see the note above ISO_Probe_Recall for
**	why it has to be.
*/
static std::string Probe_Key(std::string const & location)
{
	return("opents-iso-probe|" + location);
}


ISOLinkClass::ISOLinkClass(void)
{
	Reset();
}


void ISOLinkClass::Reset(void)
{
	Round = 0.0;
	Speed = 0.0;
}


/*
**	Moves an estimate towards a reading, quickly when the reading is the better of the two
**	and slowly when it is the worse. A link is capable of what its best readings say and no
**	more, so the floor is worth finding fast; a reading above it is as likely to be one
**	request queued behind another as it is to be the link changing, and widening every
**	window behind such a reading is how a single stall turns into a burst of over-fetching.
*/
double ISOLinkClass::Follow(double current, double sample)
{
	if (!(sample > 0.0)) return(current);
	if (!(current > 0.0)) return(sample);

	/*
	**	One reading is allowed to say the link is several times better or worse than it was
	**	believed to be, and no more. A request that queued behind another, or that arrived
	**	while a movie was decoding, is off by an order of magnitude and is a property of that
	**	moment rather than of the link; taken at face value it would widen every window
	**	behind it. A link that has really changed says so again on the next reading, and gets
	**	there in a handful of them.
	*/
	if (sample > current * SURGE) sample = current * SURGE;
	if (sample < current / SURGE) sample = current / SURGE;

	double const weight = (sample < current) ? FALL : RISE;

	return(current + (sample - current) * weight);
}


/// <summary>Takes in what one completed request cost.</summary>
/// <remarks>
/// Which of the two estimates a request says anything about is decided by what it asked
/// for. A directory sector is small enough that its whole cost is the round trip, and a
/// block or a span is large enough that what is left once the trip is taken off is the
/// rate. Between the two a request is ambiguous and is left out of both, which is what
/// keeps the estimates from feeding each other.
/// </remarks>
void ISOLinkClass::Note(std::uint64_t bytes, double milliseconds)
{
	if (bytes == 0 || !(milliseconds >= 0.0)) return;

	if (bytes <= TRIP_MAX) {
		Round = Follow(Round, milliseconds);
		return;
	}

	if (bytes >= RATE_MIN && Round > 0.0) {
		double const moving = milliseconds - Round;

		if (moving > 0.0) Speed = Follow(Speed, (double)bytes / moving);
	}
}


/// <summary>Takes on what an earlier run measured of the same link.</summary>
void ISOLinkClass::Seed(double trip, double rate)
{
	if (Round <= 0.0 && trip > 0.0) Round = trip;
	if (Speed <= 0.0 && rate > 0.0) Speed = rate;
}


/// <summary>How many blocks a run keeps in front of itself.</summary>
unsigned int ISOLinkClass::Window(void) const
{
	if (!Measured()) return((unsigned int)WINDOW_MIN);

	double const bytes = COVER * Round * Speed;
	double const blocks = bytes / (double)ISO_BLOCK_SIZE;

	if (!(blocks > (double)WINDOW_MIN)) return((unsigned int)WINDOW_MIN);
	if (blocks >= (double)WINDOW_MAX) return((unsigned int)WINDOW_MAX);

	return((unsigned int)(blocks + 0.5));
}


/// <summary>How many blocks one request asks for.</summary>
unsigned int ISOLinkClass::Span(void) const
{
	unsigned int const split = (Window() + (unsigned int)SPLIT - 1) / (unsigned int)SPLIT;

	if (split <= (unsigned int)SPAN_MIN) return((unsigned int)SPAN_MIN);
	if (split >= (unsigned int)SPAN_MAX) return((unsigned int)SPAN_MAX);

	return(split);
}


/// <summary>How many requests one image may have outstanding.</summary>
unsigned int ISOLinkClass::Flights(void) const
{
	unsigned int extra = 0;

	if (Round > 0.0) extra = (unsigned int)(Round / CROWD);

	unsigned int const flights = (unsigned int)FLIGHTS_MIN + extra;

	return((flights > (unsigned int)FLIGHTS_MAX) ? (unsigned int)FLIGHTS_MAX : flights);
}


/// <summary>How many bytes are worth taking rather than paying another trip for.</summary>
std::uint64_t ISOLinkClass::Reach(void) const
{
	std::uint64_t const floor = (std::uint64_t)ISO_BLOCK_SIZE;
	std::uint64_t const ceiling = (std::uint64_t)SPAN_MAX * (std::uint64_t)ISO_BLOCK_SIZE;

	if (!Measured()) return(floor);

	double const bytes = Round * Speed;

	if (!(bytes > (double)floor)) return(floor);
	if (bytes >= (double)ceiling) return(ceiling);

	return((std::uint64_t)bytes);
}


ISOReadAheadClass::ISOReadAheadClass(void)
{
	Reset();
}


void ISOReadAheadClass::Reset(void)
{
	Next = 0;
	Filled = 0;
	Wide = 0;
	From = 0;
	Stop = 0;
	Length = 0;
}


/// <summary>Takes on a run the file layer has declared.</summary>
void ISOReadAheadClass::Begin(std::uint64_t first, std::uint64_t stop)
{
	if (stop <= first) {
		Reset();
		return;
	}

	Next = first;
	Filled = first;
	Wide = 0;
	From = first;
	Stop = stop;

	/*
	**	Believed at once. What declared the run knows the reading is sequential, so there is
	**	no pattern left to establish; the window is still the smallest one, because nothing
	**	has been read yet and a run may reach no further than it has covered.
	*/
	Length = (unsigned int)RUN_MIN;
}


/// <summary>Would a read carry on where this run has reached?</summary>
bool ISOReadAheadClass::Continues(std::uint64_t first) const
{
	return((Length != 0) && (first + 1 >= Next) && (first <= Next));
}


/// <summary>Follows a read to the blocks it covered.</summary>
void ISOReadAheadClass::Note(std::uint64_t first, std::uint64_t last)
{
	bool const continues = Continues(first);

	Wide = last - first + 1;

	if (continues) {
		if (last + 1 > Next) {
			Length++;
			Next = last + 1;
		}
	} else {
		Length = 1;
		Next = last + 1;
		Filled = Next;
		From = first;
		Stop = 0;
	}

	if (Filled < Next) Filled = Next;
}


/// <summary>Reports the span in front of the cursor worth asking for now.</summary>
/// <remarks>
/// A run may reach no further in front of the reading than the reading has already covered,
/// so a burst that stops has over-read by at most what it read, and a run long enough to be
/// worth following reaches whatever the link is worth within a few blocks of starting.
/// A declared run also stops where its file does, which is what makes reaching a long way
/// in front of a movie cost nothing at all when the movie is watched to the end.
/// </remarks>
bool ISOReadAheadClass::Span(std::uint64_t blocks, unsigned int window, unsigned int span,
	std::uint64_t & start, std::uint64_t & count) const
{
	if (Length < (unsigned int)RUN_MIN) return(false);
	if (span == 0 || window == 0) return(false);

	std::uint64_t end = blocks;

	if (Bounded() && Stop < end) end = Stop;
	if (Filled >= end) return(false);

	/*
	**	A read that already covers a span of its own carries the round trip it costs, and
	**	the whole-block run it goes out as is one request however many blocks it wants.
	**	There is nothing for a window to hide there, and the loading that reads that way
	**	moves from file to file, so what was asked for in front of it is usually dropped.
	**	A declared run cannot run on into the next file, so it is not held back this way.
	*/
	if (!Bounded() && Wide >= (std::uint64_t)span) return(false);

	/*
	**	A run nobody declared may reach no further in front of the reading than the reading
	**	has already covered, since the only evidence it will carry on is that it has. A
	**	declared run has better evidence than that and opens its window at once: what
	**	declared it knows the reading is sequential and knows where it stops, so reaching
	**	the whole window can neither cross into another file nor over-read one that is read
	**	to the end. Earning the window a block at a time is what left a declared file paying
	**	a round trip per refill for as many blocks as the window is wide -- the file is read
	**	far faster than a distant link answers, so it outran its own window every time.
	**
	**	A declared run also reaches its own distance rather than the one the link was
	**	measured to be worth. A measurement that comes out too small is not a small mistake
	**	here: a window narrower than one of the reader's own reads can never be in front of
	**	it at all, so every read is a round trip and the estimate has no way to correct
	**	itself. What the run costs if the reading stops is bounded by the file it is inside
	**	and by what abandoning it gives back, not by the estimate.
	*/
	std::uint64_t reach = (std::uint64_t)window;
	std::uint64_t chunk = (std::uint64_t)span;

	if (!Bounded()) {
		std::uint64_t const covered = Next - From;

		if (covered < reach) reach = covered;
		if (reach < 2) reach = 2;
	} else {
		std::uint64_t const bites = Wide * (std::uint64_t)BOUND_READS;

		if (reach < (std::uint64_t)BOUND_MIN) reach = (std::uint64_t)BOUND_MIN;
		if (reach < bites) reach = bites;

		// And one request asks for more than one read takes, so a reader taking large bites
		// is not answered a bite at a time.
		if (chunk < Wide * 2) chunk = Wide * 2;
		if (chunk > reach) chunk = reach;
	}

	/*
	**	The window is refilled once it is half spent rather than topped up after every read.
	**	Topping it up asks for the one block the reading has just uncovered, which is a round
	**	trip for a block and as many of them as there were before; waiting until there is
	**	half a window to ask for is what turns them back into one request for many blocks.
	*/
	if (Filled >= Next + reach / 2) return(false);

	std::uint64_t stop = Next + reach;
	if (stop > Filled + chunk) stop = Filled + chunk;
	if (stop > end) stop = end;
	if (stop <= Filled) return(false);

	start = Filled;
	count = stop - Filled;
	return(true);
}


void ISOReadAheadClass::Issued(std::uint64_t upto)
{
	if (upto > Filled) Filled = upto;
}


ISOReadRunsClass::ISOReadRunsClass(void)
{
	Reset();
}


void ISOReadRunsClass::Reset(void)
{
	for (std::size_t place = 0; place < (std::size_t)RUNS; place++) {
		Runs[place].Reset();
		Order[place] = place;
	}

	for (std::size_t place = 0; place < (std::size_t)BOUNDS; place++) {
		Declared[place].First = 0;
		Declared[place].Stop = 0;
	}

	Written = 0;
}


/// <summary>Takes in a run of blocks the file layer says is one file.</summary>
void ISOReadRunsClass::Declare(std::uint64_t first, std::uint64_t stop)
{
	if (stop <= first) return;

	for (std::size_t place = 0; place < (std::size_t)BOUNDS; place++) {
		if (Declared[place].First == first && Declared[place].Stop == stop) return;
	}

	Declared[Written].First = first;
	Declared[Written].Stop = stop;
	Written = (Written + 1) % (std::size_t)BOUNDS;
}


/// <summary>Finds the declared file a read has landed in.</summary>
std::uint64_t ISOReadRunsClass::Bound(std::uint64_t first) const
{
	std::uint64_t found = 0;

	for (std::size_t place = 0; place < (std::size_t)BOUNDS; place++) {
		BoundType const & bound = Declared[place];

		if (bound.Stop <= bound.First) continue;
		if (first < bound.First || first >= bound.Stop) continue;

		/*
		**	The narrowest declaration covering the read is the one that describes it. A
		**	mixfile and the file embedded in it are both declared, and it is the embedded
		**	one that says where the reading stops.
		*/
		if (found == 0 || bound.Stop < found) found = bound.Stop;
	}

	return(found);
}


/// <summary>Follows a read to the run it belongs to, which becomes the current one.</summary>
/// <remarks>
/// Displacing the run that has gone longest without a read is what bounds the set. Only that
/// run's outstanding span is reported: the others are still being read along and what was
/// asked for in front of them is still wanted.
/// </remarks>
bool ISOReadRunsClass::Note(std::uint64_t first, std::uint64_t last, std::uint64_t & lost, std::uint64_t & stop)
{
	std::size_t place = 0;

	while (place < (std::size_t)RUNS && !Runs[Order[place]].Continues(first)) place++;

	bool displaced = false;

	std::uint64_t bound = 0;

	if (place == (std::size_t)RUNS) {
		place = (std::size_t)RUNS - 1;

		ISOReadAheadClass & oldest = Runs[Order[place]];

		if (oldest.Edge() > oldest.Cursor()) {
			lost = oldest.Cursor();
			stop = oldest.Edge();
			displaced = true;
		}

		oldest.Reset();

		/*
		**	A read starting a run inside a file the layer declared takes that file's end
		**	with it, and the run is believed from this first read rather than from the
		**	second: the declaration is what says the reading is sequential, so there is no
		**	pattern left to establish.
		*/
		bound = Bound(first);
		if (bound > last + 1) oldest.Begin(first, bound);
	}

	if (place != 0) {
		std::size_t const chosen = Order[place];

		for (std::size_t step = place; step > 0; step--) Order[step] = Order[step - 1];
		Order[0] = chosen;
	}

	Runs[Order[0]].Note(first, last);
	return(displaced);
}


/*
**	What the images have cost so far. Requests counts round trips and Fetched the bytes they
**	carried, both across every source; Touched marks every block-sized window a request has
**	covered, and holds one set of them per image, since the same block number on two discs
**	names two different blocks. Their combined population is the working set -- the part of
**	the discs the game turns out to need, which is the figure that says whether reading on
**	demand was worth it.
*/
static unsigned int _Requests = 0;
static std::uint64_t _Fetched = 0;
static std::vector<std::vector<bool>> _Touched;

// Every image currently open, so that one of them can write out a batch another has been
// left holding.
static std::vector<ISOHttpSourceClass *> _Open;

/*
**	And what the store saved. Hits counts the blocks it answered for and Bytes what they
**	carried, Kept the blocks written to it, and State what became of it -- which is the only
**	way a page can tell a store that was never reached from one that was given up.
*/
static unsigned int _StoreHits = 0;
static std::uint64_t _StoreBytes = 0;
static unsigned int _StoreKept = 0;
static unsigned int _StoreState = 0;
static unsigned int _StoreDiscarded = 0;

/*
**	And what the look-ahead cost and saved. Requests and Bytes are the part of the figures
**	above that was asked for before anything wanted it; Served counts the reads it answered
**	and Waited the few of those that reached the read before the answer did. Waste is the
**	bytes it asked for that no read ever took, which is what the guessing cost the
**	connection and is the figure that says whether the window is aimed too far ahead.
*/
static unsigned int _AheadRequests = 0;
static std::uint64_t _AheadBytes = 0;
static unsigned int _AheadServed = 0;
static unsigned int _AheadWaited = 0;
static std::uint64_t _AheadWaste = 0;

/*
**	And the part of that which was asked for on the engine's word that it would probably be
**	wanted, rather than on where a run was heading. It is counted separately because it is
**	the only fetching a player can be left waiting for nothing at all: a run reads on and
**	takes what was asked for in front of it, while a menu the player walks away from takes
**	none of what was fetched for the choice they did not make.
*/
static unsigned int _SoonRequests = 0;
static std::uint64_t _SoonBytes = 0;

/*
**	And what the file layer said, which is the difference between a run that had to be
**	noticed and one that was declared. Runs counts the files it named as they were opened,
**	Soon the ones it said would be wanted later, and Done the runs it gave up on.
*/
static unsigned int _HintRuns = 0;
static unsigned int _HintSoon = 0;
static unsigned int _HintDone = 0;

/*
**	And what the reading cost the player in time they spent looking at nothing. A read the
**	block cache or a landed span answers costs nothing and is not counted; a read that goes
**	to the network, waits on a span that has not landed, or waits on the store is counted
**	for as long as the engine sits in it. The count says whether it was many small hitches
**	or a few long ones and the worst says whether any of them was long enough to be seen,
**	which is the difference between a run that is slow and a run that stops.
*/
/*
**	And the reads that took none of it. A reader that may go without says so rather than
**	waiting, so each of these is a stall that did not happen; the blocks are asked for at
**	the same moment and the read comes back for them once they have landed.
*/
static unsigned int _Deferred = 0;

static unsigned int _Stalls = 0;
static double _StallMs = 0.0;
static double _StallWorst = 0.0;
static std::uint64_t _StallWorstAt = 0;
static unsigned int _StallWorstLength = 0;

// Where a stall was spent. A wait on the store is the engine waiting on a read the same way
// a request is, so the record calls both of them what they are to the player.
enum StallSourceType {
	STALL_TRANSFER,		// A synchronous request the engine waited on.
	STALL_AHEAD,		// A look-ahead span that had not landed yet.
	STALL_STORE			// The browser's database.
};

// And the location of every image opened, so a stall says which disc it was on rather than
// which slot the disc happens to occupy.
static std::vector<std::string> _Images;


/*
**	Hands one stall to the page as it happens, as a line of text on OpenTS_State.
**
**	It is put there rather than behind an exported function because a page cannot call one:
**	the browser configuration links only the runtime methods it needs, so neither the heap
**	nor the string helpers are reachable from outside the module and a pointer returned to
**	the page could not be read. What is written here is written from inside the module's own
**	glue, where both are in scope, and what the page sees is an ordinary array of strings.
**
**	Only the leading number is meant to be parsed. A reader that sees it skip knows lines
**	were dropped, which is what bounding the array costs and what makes bounding it safe.
*/
EM_JS(void, ISO_Stall_Record, (char const * line, double seconds), {
	var state = globalThis.OpenTS_State;

	if (state === undefined || state === null) {
		state = {};
		globalThis.OpenTS_State = state;
	}

	if (!state.stalls) state.stalls = [];

	state.stalls.push(UTF8ToString(line));
	state.stallSeconds = seconds;

	// A long session stalls more often than a page has any use for, so the oldest go. The
	// sequence number in each line is what says so.
	if (state.stalls.length > 4096) state.stalls.splice(0, state.stalls.length - 4096);
});


static void Account_For_Stall(std::size_t meter, std::uint64_t offset, unsigned int length,
	double milliseconds, StallSourceType source)
{
	if (!(milliseconds > 0.0)) return;

	double const began = emscripten_get_now() - milliseconds;

	_Stalls++;
	_StallMs += milliseconds;

	if (milliseconds > _StallWorst) {
		_StallWorst = milliseconds;
		_StallWorstAt = offset;
		_StallWorstLength = length;
	}

	/*
	**	The disc is named by the last part of its location, since that is what a player and a
	**	log both call it. Whitespace would break a line into the wrong number of fields, so
	**	what little of it a location may carry is replaced rather than passed on.
	*/
	std::string name = (meter < _Images.size()) ? _Images[meter] : std::string();
	std::size_t const cut = name.find_last_of('/');

	if (cut != std::string::npos) name.erase(0, cut + 1);
	if (name.size() > 64) name.erase(64);
	if (name.empty()) name = "disc";

	for (char & letter : name) {
		if ((unsigned char)letter <= ' ') letter = '_';
	}

	char line[192];

	std::snprintf(line, sizeof(line), "%u %.3f %s %llu %u %.1f %s", _Stalls, began / 1000.0,
		name.c_str(), (unsigned long long)offset, length, milliseconds,
		(source == STALL_AHEAD) ? "ahead" : "demand");

	ISO_Stall_Record(line, _StallMs / 1000.0);
}

/*
**	And what the link turned out to cost, taken from whichever image measured last. The
**	discs are served from one place, so one reading describes them all closely enough to be
**	worth reporting; what each image acts on is its own.
*/
static double _LinkTrip = 0.0;
static double _LinkRate = 0.0;
static unsigned int _LinkWindow = 0;

/*
**	And how the images were established: how many were asked about, and how many the browser
**	could already say what they were. A warm launch that reports a probe has been given a
**	location it had not been given before.
*/
static unsigned int _Probes = 0;
static unsigned int _Recalls = 0;


static std::size_t Account_For_Image(char const * url)
{
	_Touched.emplace_back();
	_Images.emplace_back(url != nullptr ? url : "");
	return(_Touched.size() - 1);
}


static void Account_For_Transfer(std::size_t meter, std::uint64_t offset, unsigned int length)
{
	_Requests++;
	_Fetched += length;

	if (meter >= _Touched.size()) return;

	std::vector<bool> & touched = _Touched[meter];
	std::uint64_t const first = offset / ISOHttpSourceClass::BLOCK_SIZE;
	std::uint64_t const last = (offset + length - 1) / ISOHttpSourceClass::BLOCK_SIZE;

	if (touched.size() <= last) {
		touched.resize((std::size_t)last + 1, false);
	}

	for (std::uint64_t index = first; index <= last; index++) {
		touched[(std::size_t)index] = true;
	}
}


extern "C" {

EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Requests(void) {return(_Requests);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Fetched(void) {return((double)_Fetched);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Block_Size(void) {return(ISOHttpSourceClass::BLOCK_SIZE);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Images(void) {return((unsigned int)_Touched.size());}

EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Unique_Blocks(void)
{
	unsigned int count = 0;

	for (std::vector<bool> const & image : _Touched) {
		for (bool touched : image) {
			if (touched) count++;
		}
	}

	return(count);
}

/*
** 0 the store was never reached, 1 it is serving, 2 it was given up, 3 it is serving what it
** has and taking nothing more, because the origin refused a write. A run that reports 0 with
** the scaffold built in never got as far as a read underneath main.
*/
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Store_State(void) {return(_StoreState);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Store_Hits(void) {return(_StoreHits);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Store_Bytes(void) {return((double)_StoreBytes);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Store_Kept(void) {return(_StoreKept);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Store_Discarded(void) {return(_StoreDiscarded);}

EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Ahead_Requests(void) {return(_AheadRequests);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Ahead_Bytes(void) {return((double)_AheadBytes);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Ahead_Served(void) {return(_AheadServed);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Ahead_Waited(void) {return(_AheadWaited);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Ahead_Waste(void)
{
#if defined(OPENTS_WASM_JSPI)
	return((double)_AheadWaste + ISO_Http_Ahead_Unread());
#else
	return((double)_AheadWaste);
#endif
}

/*
** What the reading cost in time the player spent waiting: how many reads stopped the engine,
** how long they stopped it for in total, and how long the worst of them lasted. All three
** are cumulative for the run and are zero before a disc is read, which is what a build whose
** data is preloaded reports for the whole of it. What is reported is the time the engine sat
** in the read, not how long the request took: a request the look-ahead started early and that
** landed before anything wanted it costs the engine nothing and is counted as nothing.
*/
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Stalls(void) {return(_Stalls);}

/*
** And how many reads declined instead of stalling. A reader that can carry on without the
** bytes is answered rather than made to wait, so this is the count of stalls that were
** available to be taken and were not.
*/
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Deferred(void) {return(_Deferred);}

EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Stall_Ms(void) {return(_StallMs);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Stall_Worst(void) {return(_StallWorst);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Stall_Worst_Offset(void) {return((double)_StallWorstAt);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Stall_Worst_Length(void) {return(_StallWorstLength);}

/*
** The stalls themselves are not exported. They are pushed onto OpenTS_State.stalls as they
** happen, one line of text each, with OpenTS_State.stallSeconds beside them carrying the
** running total; see ISO_Stall_Record for why they go there rather than through a function.
*/

EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Hint_Runs(void) {return(_HintRuns);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Hint_Done(void) {return(_HintDone);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Hint_Soon(void) {return(_HintSoon);}

EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Soon_Requests(void) {return(_SoonRequests);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Soon_Bytes(void) {return((double)_SoonBytes);}

/*
** How the discs were established. A run that opened every image out of what the browser
** already held reports as many recalls as it has images and no probes at all, which is what
** a launch whose locations are unchanged looks like.
*/
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Probes(void) {return(_Probes);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Recalls(void) {return(_Recalls);}

/*
** What the link to the discs turns out to cost, as the read ahead measures it: the round
** trip in milliseconds, the rate in bytes a millisecond, and the blocks a run is reaching
** in front of itself because of them. A page reporting a window of two on a fast link and
** a window of ninety on a slow one is the adaptation working.
*/
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Link_Trip(void) {return(_LinkTrip);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Link_Rate(void) {return(_LinkRate);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Link_Window(void) {return(_LinkWindow);}

}


ISOHttpSourceClass::ISOHttpSourceClass(void) :
	Length(0),
	Meter(0),
	Queued(0),
	FromRecord(false),
	Learned(0.0),
	StoreState(STORE_UNTRIED),
	Staged(0),
	StagedAt(0.0)
{
}


ISOHttpSourceClass::~ISOHttpSourceClass(void)
{
	Close();
}


/// <summary>Asks the server what the image is, and keeps the answer.</summary>
bool ISOHttpSourceClass::Probe(void)
{
	std::vector<char> validator(ISOBlockIndexClass::SIGNATURE_MAX);
	validator[0] = '\0';

	double const began = emscripten_get_now();
	double const length = ISO_Http_Probe(Url.c_str(), validator.data(), (int)validator.size());

	_Probes++;

	if (!(length > 0.0)) return(false);

	/*
	**	The probe asks for one byte, so what it cost is a round trip and nothing else. It is
	**	the first thing known about the link and the only reading available before the engine
	**	has read anything, which is what stops the first file being read as though the server
	**	were on this machine.
	*/
	Link.Note(1, emscripten_get_now() - began);
	_LinkTrip = Link.Trip();

	Length = (std::uint64_t)length;
	Validator = validator.data();

	Learn();
	return(true);
}


/// <summary>Writes what is known about the image back to where a later run reads it.</summary>
void ISOHttpSourceClass::Learn(void)
{
	if (Location.empty() || Length == 0) return;

	ISOProbeClass known;

	known.Length = Length;
	known.Validator = Validator;
	known.Trip = Link.Trip();
	known.Rate = Link.Rate();

	ISO_Probe_Remember(Probe_Key(Location).c_str(), known.Encode().c_str());
	Learned = emscripten_get_now();
}


/// <summary>Opens an image served from a URL.</summary>
/// <param name="url">Where the image is, absolute or relative to the page.</param>
/// <returns>bool; Is the image's length known, from a stored record or from the server?</returns>
/// <remarks>
/// The store is not opened here. Open is reached from the first file the host cannot answer
/// for, which the engine asks about while its static objects are still being constructed,
/// and a wait there is not yet legal. That is also why the record this reads is kept in a
/// store that answers without one, and why the record has to carry the length rather than
/// leaving it to be looked up beside the blocks.
/// </remarks>
bool ISOHttpSourceClass::Open(char const * url)
{
	Close();

	if (url == nullptr || *url == '\0') return(false);

	Url = url;

	std::vector<char> identity(ISOBlockIndexClass::SIGNATURE_MAX);
	identity[0] = '\0';
	ISO_Http_Identity(Url.c_str(), identity.data(), (int)identity.size());

	Location = (identity[0] != '\0') ? identity.data() : Url;

	/*
	**	What an earlier run was told about this location, which is everything a probe would
	**	answer. Nothing about a file on a server changes while the location does not, so a
	**	launch whose locations are unchanged asks the server nothing at all.
	*/
	ISOProbeClass known;
	std::vector<char> record(ISOProbeClass::RECORD_MAX);

	record[0] = '\0';
	FromRecord = (ISO_Probe_Recall(Probe_Key(Location).c_str(), record.data(),
		(int)record.size()) == 1) && known.Decode(record.data());

	if (FromRecord) {
		_Recalls++;
		Length = known.Length;
		Validator = known.Validator;
		Link.Seed(known.Trip, known.Rate);
		_LinkTrip = Link.Trip();
		_LinkRate = Link.Rate();
		_LinkWindow = Link.Window();
	} else if (!Probe()) {
		Url.clear();
		Location.clear();
		Validator.clear();
		return(false);
	}

	Meter = Account_For_Image(Url.c_str());
	_Open.push_back(this);

	Signature = ISOBlockIndexClass::Signature(Location.c_str(), Length, Validator.c_str());
	Slot = ISOBlockIndexClass::Store_Slot(Location.c_str());

	Watch();
	return(true);
}


/// <summary>Asks, once the run is under way, whether the image still is what was believed.</summary>
void ISOHttpSourceClass::Watch(void)
{
	if (!FromRecord || Location.empty()) return;

	ISO_Probe_Watch(Probe_Key(Location).c_str(), Url.c_str(), (double)Length,
		Validator.c_str(), WATCH_DELAY);
}


/// <summary>Re-establishes an image a stored record described wrongly.</summary>
bool ISOHttpSourceClass::Revive(void)
{
	if (!FromRecord) return(false);

	/*
	**	Once, whatever comes of it. A record the server disagrees with is gone either way,
	**	and a read that fails a second time is a read that fails.
	*/
	FromRecord = false;
	ISO_Probe_Forget(Probe_Key(Location).c_str());

	std::uint64_t const believed = Length;
	std::string const named = Validator;

	if (!Probe()) return(false);
	if (Length == believed && Validator == named) return(false);

	/*
	**	A different image, so the blocks held under this slot are another file's sectors and
	**	the signature no longer answers to them. Letting go of the index is what stops them
	**	being served; the slot itself is cleared when the store is next opened and the record
	**	in it turns out to have been written for something else.
	*/
	Signature = ISOBlockIndexClass::Signature(Location.c_str(), Length, Validator.c_str());
	Store_Discard();
	Index.Reset(Signature);
	StoreState = STORE_UNTRIED;
	_StoreState = 0;

	return(true);
}


void ISOHttpSourceClass::Close(void)
{
	/*
	**	Whatever the batch is still holding is dropped rather than written. Close is reached
	**	from the destructor, and a destructor may run while the module is being torn down,
	**	where a wait is no longer legal. The blocks are on the server either way.
	*/
	Store_Discard();
	Ahead_Drop();

	_Open.erase(std::remove(_Open.begin(), _Open.end(), this), _Open.end());

	Url.clear();
	Location.clear();
	Validator.clear();
	Signature.clear();
	Slot.clear();
	Removals.clear();
	Length = 0;
	Cache.clear();
	Index.Reset(std::string());
	Ahead.Reset();
	Link.Reset();
	Queued = 0;
	FromRecord = false;
	Learned = 0.0;
	StoreState = STORE_UNTRIED;
	Staged = 0;
}


/*
**	------------------------------------------------------------------------------------
**	The look-ahead, as the source uses it. Without the suspension scaffold the engine never
**	hands the page the thread, so nothing a request was left to answer could ever land and
**	every byte of it would be waste: the whole of it is behind the scaffold for that reason
**	rather than because starting a request is a wait, which it is not.
**	------------------------------------------------------------------------------------
*/

/// <summary>Asks for the span the run is about to want, without waiting for it.</summary>
/// <remarks>Reached from a read the network had to carry, so what the window covers is a
/// stretch of the image the browser is not already holding. Blocks the store does hold cost
/// nothing to read, so the window steps over them rather than paying for them again, and
/// what is asked for is the run of missing blocks in front of the cursor.</remarks>
void ISOHttpSourceClass::Look_Ahead(void)
{
#if defined(OPENTS_WASM_JSPI)
	if (Length == 0) return;

	/*
	**	Before main the engine reaches no yield, so a request left in flight has nothing to
	**	land in and every byte of it would be waste.
	*/
	if (ISO_Store_Under_Main() == 0) return;

	ISO_Http_Ahead_Ready((unsigned int)BLOCK_SIZE);

	unsigned int window = Link.Window();
	unsigned int span = Link.Span();

	/*
	**	What the guessing has already cost and nobody has taken. Over the share it is
	**	allowed, the window closes to a single request and no further: one request is the
	**	least that still covers a round trip, and a window narrower than that stalls the
	**	streaming the window exists for. What is given up is the long reach, which only
	**	pays off when the guess is right and is exactly what being over the share says it
	**	has not been.
	*/
	if (_Fetched > WASTE_FLOOR) {
		double const unread = (double)_AheadWaste + ISO_Http_Ahead_Unread();

		if (unread > WASTE_SHARE * (double)_Fetched) {
			window = (unsigned int)ISOLinkClass::SPAN_MIN;
			span = (unsigned int)ISOLinkClass::SPAN_MIN;
		}
	}

	/*
	**	The window is being refilled, so the connection belongs to the reading. Whatever was
	**	asked for on a guess is let go rather than left to finish in front of this, which is
	**	the whole of what keeps a guess from ever being the reason a frame is late.
	*/
	double const given = ISO_Http_Ahead_Busy(Url.c_str(), 1);

	if (given > 0.0) _AheadWaste += (std::uint64_t)given;

	std::uint64_t const blocks = (Length + (std::uint64_t)BLOCK_SIZE - 1) / (std::uint64_t)BLOCK_SIZE;
	unsigned int const flights = Link.Flights();

	ISOReadAheadClass & run = Ahead.Current();

	/*
	**	The window is filled here rather than one request per read that missed. A run opened
	**	by a read whose file is declared wants the whole window at once, and asking for it in
	**	several requests beside each other costs one round trip for all of them where asking
	**	for one and waiting for the next read costs one apiece.
	*/
	for (unsigned int flight = 0; flight < flights; flight++) {

		std::uint64_t start = 0;
		std::uint64_t count = 0;

		if (!run.Span(blocks, window, span, start, count)) return;

		while (count > 0 && Index.Holds(start)) {
			start++;
			count--;
			run.Issued(start);
		}

		std::uint64_t missing = 0;
		while (missing < count && !Index.Holds(start + missing)) missing++;

		if (missing == 0) return;

		std::uint64_t const at = start * (std::uint64_t)BLOCK_SIZE;
		std::uint64_t bytes = missing * (std::uint64_t)BLOCK_SIZE;

		if (at + bytes > Length) bytes = Length - at;

		double const asked = ISO_Http_Ahead_Start(Url.c_str(), (double)at, (double)bytes,
			(int)flights);

		/*
		**	A request that was declined -- one image already has as much in flight as it is
		**	allowed -- leaves the window where it is, so the blocks are asked for at the next
		**	read rather than skipped and paid for at full price when the reading reaches them.
		*/
		if (asked == 0.0) return;

		run.Issued(start + missing);

		if (asked > 0.0) {
			Account_For_Transfer(Meter, at, (unsigned int)asked);
			_AheadRequests++;
			_AheadBytes += (std::uint64_t)asked;
		}
	}
#endif
}


/// <summary>Asks for the blocks a read wants, without waiting for any of them.</summary>
/// <param name="offset">Where the span the read could not be served begins.</param>
/// <param name="length">How long it is.</param>
/// <remarks>The window reaches in front of the cursor and never asks for the block the
/// reading is standing on, because an ordinary read fetches that one itself on its way
/// through. A read that declines fetches nothing, so unless the block it wanted is asked
/// for here nothing would ever ask for it and the reader would decline for ever. A block
/// already held or already on its way is left alone.</remarks>
void ISOHttpSourceClass::Ahead_Want(std::uint64_t offset, unsigned int length)
{
#if defined(OPENTS_WASM_JSPI)
	if (Url.empty() || Length == 0 || length == 0) return;
	if (ISO_Store_Under_Main() == 0) return;

	ISO_Http_Ahead_Ready((unsigned int)BLOCK_SIZE);

	std::uint64_t const first = offset / (std::uint64_t)BLOCK_SIZE;
	std::uint64_t const last = (offset + length - 1) / (std::uint64_t)BLOCK_SIZE;

	std::uint64_t const at = first * (std::uint64_t)BLOCK_SIZE;
	std::uint64_t bytes = (last - first + 1) * (std::uint64_t)BLOCK_SIZE;

	if (at + bytes > Length) bytes = Length - at;
	if (bytes == 0) return;

	if (ISO_Http_Ahead_State(Url.c_str(), (double)at, (unsigned int)bytes) != 0) return;

	double const asked = ISO_Http_Ahead_Start(Url.c_str(), (double)at, (double)bytes,
		(int)Link.Flights());

	if (asked > 0.0) {
		Account_For_Transfer(Meter, at, (unsigned int)asked);
		_AheadRequests++;
		_AheadBytes += (std::uint64_t)asked;
	}
#else
	(void)offset;
	(void)length;
#endif
}


/// <summary>Is any of what a read wants already on its way?</summary>
/// <param name="offset">Where the read begins.</param>
/// <param name="length">How long it is.</param>
/// <returns>bool; Is a request outstanding that covers part of it?</returns>
/// <remarks>A read that may decline and whose bytes are already coming has nothing to do
/// but come back later, and doing anything at all would be worse than doing nothing: it
/// asks again for what is already in flight, and it makes the image follow a run that is
/// not being read, which takes the window off the reading that is.</remarks>
bool ISOHttpSourceClass::Ahead_Pending(std::uint64_t offset, unsigned int length)
{
#if defined(OPENTS_WASM_JSPI)
	if (Url.empty() || Length == 0 || length == 0) return(false);

	std::uint64_t const first = offset / (std::uint64_t)BLOCK_SIZE;
	std::uint64_t const last = (offset + length - 1) / (std::uint64_t)BLOCK_SIZE;

	for (std::uint64_t index = first; index <= last; index++) {
		std::uint64_t const at = index * (std::uint64_t)BLOCK_SIZE;
		std::uint64_t span = (std::uint64_t)BLOCK_SIZE;

		if (at + span > Length) span = Length - at;

		if (ISO_Http_Ahead_State(Url.c_str(), (double)at, (unsigned int)span) == 1) return(true);
	}

	return(false);
#else
	(void)offset;
	(void)length;
	return(false);
#endif
}


/// <summary>Serves a span the look-ahead already asked for.</summary>
/// <returns>bool; Was the whole span delivered without a request of its own?</returns>
/// <remarks>A span whose bytes are already here costs a copy and no suspension at all,
/// which is the case the window exists to produce. One still in flight is waited on, and
/// that wait is legal only underneath the promising export.</remarks>
bool ISOHttpSourceClass::Ahead_Serve(std::uint64_t offset, void * buffer, unsigned int length,
	ReadType const & read)
{
#if defined(OPENTS_WASM_JSPI)
	if (Url.empty()) return(false);

	/*
	**	Reading, though not at anything's expense: a span the pool is already holding costs
	**	no request at all. It still says the image is in use, which is what the guessing
	**	waits to stop hearing.
	*/
	ISO_Http_Ahead_Busy(Url.c_str(), 0);

	int const state = ISO_Http_Ahead_State(Url.c_str(), (double)offset, length);

	if (state == 2) {
		if (ISO_Http_Ahead_Copy(Url.c_str(), (double)offset, buffer, length) != 1) return(false);
		_AheadServed++;
		return(true);
	}

	if (state != 1 || ISO_Store_Under_Main() == 0) return(false);

	/*
	**	The bytes are on their way but are not here. A read that may decline says so and
	**	comes back for them later; only a read that must have them waits.
	*/
	if (ISODeferredReadClass::Deferring()) return(false);

	double const stalled = emscripten_get_now();

	if (ISO_Http_Ahead_Wait(Url.c_str(), (double)offset, buffer, length) != 1) return(false);

	Account_For_Stall(Meter, read.Offset, read.Length, emscripten_get_now() - stalled, STALL_AHEAD);
	_AheadServed++;
	_AheadWaited++;
	return(true);
#else
	return(false);
#endif
}


/// <summary>Abandons everything asked for ahead of this image.</summary>
void ISOHttpSourceClass::Ahead_Drop(void)
{
#if defined(OPENTS_WASM_JSPI)
	if (Url.empty()) return;

	double const wasted = ISO_Http_Ahead_Drop(Url.c_str());

	if (wasted > 0.0) _AheadWaste += (std::uint64_t)wasted;
#endif
}


/// <summary>Takes in what the file layer says about a run of the image.</summary>
/// <remarks>
/// A sequential hint is a file being opened, and it says the two things a block source
/// cannot work out: which way the reading is about to go, and where it stops. The run is
/// taken up at once rather than after the reads it would take to notice it, and nothing in
/// front of it is ever asked for past the end of the file. Nothing is fetched here -- the
/// first read does that, and by then it is a read into a run that is already believed, so
/// its own request and the one in front of it go out together.
///
/// A hint that something will be wanted later is queued instead, and is fetched only while
/// the image is not being read at all.
///
/// A hint that a run is finished with is the only one that undoes work. What was asked for
/// in front of that run is bytes nobody is going to read, so the requests are abandoned at
/// once rather than left to finish; the whole blocks that already arrived are kept, since
/// they are paid for either way and the store can have them.
/// </remarks>
void ISOHttpSourceClass::Hint(ISOHintType kind, std::uint64_t offset, std::uint64_t length)
{
	if (Length == 0 || length == 0 || offset >= Length) return;

	if (length > Length - offset) length = Length - offset;

	if (kind == ISO_HINT_SOON) {
		_HintSoon++;
		Soon(offset, length);
		return;
	}

	if (kind == ISO_HINT_DONE) {
		_HintDone++;
		Ahead_Drop(offset / (std::uint64_t)BLOCK_SIZE,
			(offset + length + (std::uint64_t)BLOCK_SIZE - 1) / (std::uint64_t)BLOCK_SIZE);
		return;
	}

	_HintRuns++;

	std::uint64_t const first = offset / (std::uint64_t)BLOCK_SIZE;
	std::uint64_t const stop = (offset + length + (std::uint64_t)BLOCK_SIZE - 1) / (std::uint64_t)BLOCK_SIZE;

	Ahead.Declare(first, stop);
}


/// <summary>Queues a run the engine says it will probably want before long.</summary>
/// <remarks>
/// How much of the run is worth having is settled before it gets here: the engine says what
/// it is going to do with the name and the file layer turns that into the bytes, so nothing
/// here weighs one run against another by size. What this decides is only which of those
/// bytes still have to be fetched.
///
/// Blocks the store already holds are stepped over, since fetching them again would cost the
/// connection and save nothing. A run the store holds in patches is queued as the patches
/// that are missing rather than as the whole of it, which is what makes an interrupted first
/// launch cost the second one only the part it never got. The number of patches is bounded,
/// so a badly holed run is asked for as a few spans covering some blocks already held rather
/// than as a hundred requests.
/// </remarks>
void ISOHttpSourceClass::Soon(std::uint64_t offset, std::uint64_t length)
{
#if defined(OPENTS_WASM_JSPI)
	if (ISO_Store_Under_Main() == 0) return;
	if (Queued >= SOON_BUDGET) return;

	/*
	**	Nothing is guessed at that cannot be kept. A guess pays for itself in the store and
	**	nowhere else -- the pool holds only a few of them and lets go of the rest -- so with
	**	no store to bank it in the guessing is bandwidth taken off the reading for nothing.
	*/
	if (!Store_Ready() || StoreState != STORE_READY) return;

	std::uint64_t const first = offset / (std::uint64_t)BLOCK_SIZE;
	std::uint64_t const stop = (offset + length + (std::uint64_t)BLOCK_SIZE - 1) / (std::uint64_t)BLOCK_SIZE;

	ISO_Http_Ahead_Ready((unsigned int)BLOCK_SIZE);

	std::uint64_t cursor = first;
	unsigned int runs = 0;

	while (cursor < stop && runs < (unsigned int)SOON_RUNS && Queued < SOON_BUDGET) {

		while (cursor < stop && Index.Holds(cursor)) cursor++;
		if (cursor >= stop) break;

		std::uint64_t edge = cursor;

		while (edge < stop && !Index.Holds(edge)) edge++;

		/*
		**	The last patch runs to the end of the file rather than stopping where the store
		**	next holds a block. Cutting the tail into every one of its holes would spend more
		**	round trips than the blocks between them are worth, and the ones already held are
		**	stepped over again when the batch is staged.
		*/
		if (runs + 1 == (unsigned int)SOON_RUNS) edge = stop;

		std::uint64_t const at = cursor * (std::uint64_t)BLOCK_SIZE;
		std::uint64_t bytes = (edge - cursor) * (std::uint64_t)BLOCK_SIZE;

		if (at + bytes > Length) bytes = Length - at;
		if (bytes == 0) break;

		if (Queued + bytes > SOON_BUDGET) bytes = SOON_BUDGET - Queued;

		/*
		**	One round trip's worth of bytes per request. Asking for less spends more trips
		**	than the bytes are worth; asking for more leaves a larger request to abandon when
		**	the reading resumes, and abandoning it is what keeps a guess out of a run's way.
		*/
		double const queued = ISO_Http_Idle_Add(Url.c_str(), (double)at, (double)bytes,
			(double)Link.Reach(), (int)SOON_QUEUE);

		if (queued == 0.0) break;

		Queued += (std::uint64_t)queued;
		cursor = edge;
		runs++;
	}
#else
	(void)offset;
	(void)length;
#endif
}


/// <summary>Banks whatever the guessing has fetched and nothing has read.</summary>
/// <remarks>
/// A guess that nobody takes is bandwidth spent for nothing, and holding it in the pool only
/// postpones that: the span is dropped when the run ends and the bytes go with it. Putting
/// it in the store instead makes the guess worth having even when it was wrong about this
/// run -- the archive is on disc for the next launch, which is where most of a second run's
/// speed comes from -- and it is what lets the pool let go of it.
///
/// Only a little is taken per read. Every byte of it is copied and staged, and doing the
/// whole of a queue at once inside one read would cost the frame the read is on.
/// </remarks>
void ISOHttpSourceClass::Soon_Keep(void)
{
#if defined(OPENTS_WASM_JSPI)
	if (!Store_Ready() || StoreState != STORE_READY) {

		/*
		**	A store that has stopped taking blocks leaves the guessing nowhere to put them,
		**	so what was queued is given up rather than left holding the pool shut.
		*/
		if (StoreState == STORE_FULL || StoreState == STORE_OFF) {
			double const wasted = ISO_Http_Idle_Cancel(Url.c_str());

			if (wasted > 0.0) _AheadWaste += (std::uint64_t)wasted;
		}

		return;
	}

	static std::vector<unsigned char> harvest((std::size_t)BLOCK_SIZE * (std::size_t)SOON_KEEP);

	double at = 0.0;
	double const taken = ISO_Http_Idle_Take(Url.c_str(), harvest.data(),
		(unsigned int)harvest.size(), (unsigned int)BLOCK_SIZE, &at);

	if (!(taken > 0.0)) return;

	Store_Keep((std::uint64_t)at, harvest.data(), (unsigned int)taken,
		ISOBlockIndexClass::ADMIT_GUESS);
#endif
}


/// <summary>Abandons what was asked for in front of one run that has been displaced.</summary>
/// <param name="first">The first block of the displaced run's outstanding span.</param>
/// <param name="stop">The block that span ends before.</param>
void ISOHttpSourceClass::Ahead_Drop(std::uint64_t first, std::uint64_t stop)
{
#if defined(OPENTS_WASM_JSPI)
	if (Url.empty() || stop <= first) return;

	double const wasted = ISO_Http_Ahead_Drop_Range(Url.c_str(),
		(double)(first * (std::uint64_t)BLOCK_SIZE), (double)(stop * (std::uint64_t)BLOCK_SIZE));

	if (wasted > 0.0) _AheadWaste += (std::uint64_t)wasted;
#else
	(void)first;
	(void)stop;
#endif
}


/*
**	------------------------------------------------------------------------------------
**	The store, as the source uses it. Without the suspension scaffold none of it can be
**	reached, and the source reads exactly as it did before.
**	------------------------------------------------------------------------------------
*/

/// <summary>Is the store open and serving this image?</summary>
/// <remarks>
/// The first call that finds a wait legal opens the database and reads the record describing
/// what it holds. A record written for another image is not evidence about this one, so the
/// blocks it describes are cleared rather than trusted; nothing else in the engine can tell
/// the difference between a stale sector and a real one.
///
/// It is also where the image learns how much it may keep. The figure comes from what the
/// origin says it is allowed rather than from a constant, because the constant has to be
/// small enough for the smallest quota a browser might offer and that is far smaller than
/// what a machine with room will hold. A browser that will not say leaves the constant in
/// place. A record describing more than the ceiling now allows is cut down to it here, so a
/// run that starts with a smaller allowance than the one before it lets go of the difference
/// rather than carrying a store it is no longer allowed.
/// </remarks>
bool ISOHttpSourceClass::Store_Ready(void)
{
#if defined(OPENTS_WASM_JSPI)
	if (StoreState == STORE_READY || StoreState == STORE_FULL) return(true);
	if (StoreState == STORE_OFF) return(false);

	if (Signature.empty() || Slot.empty() || ISO_Store_Wanted() == 0) {
		StoreState = STORE_OFF;
		_StoreState = 2;
		return(false);
	}

	if (ISO_Store_Under_Main() == 0) return(false);

	std::vector<char> record(ISOBlockIndexClass::RECORD_MAX);
	record[0] = '\0';

	if (ISO_Store_Open(Slot.c_str(), record.data(), (int)record.size()) != 1) {
		StoreState = STORE_OFF;
		_StoreState = 2;
		return(false);
	}

	double const room = ISO_Store_Room();
	std::uint64_t ceiling = ISOBlockIndexClass::STORE_LIMIT;

	if (room > 0.0) {
		double const share = room * ISOBlockIndexClass::STORE_SHARE;

		ceiling = (share >= (double)ISOBlockIndexClass::STORE_MAX)
			? ISOBlockIndexClass::STORE_MAX : (std::uint64_t)share;
	}

	std::vector<std::uint64_t> evicted;

	if (!Index.Adopt(record.data(), Signature)) {
		_StoreDiscarded++;
		Index.Cap(ceiling, evicted);

		if (ISO_Store_Write(Slot.c_str(), Index.Encode().c_str(), "*") != 1) {
			StoreState = STORE_OFF;
			_StoreState = 2;
			return(false);
		}

		StoreState = STORE_READY;
		_StoreState = 1;
		return(true);
	}

	Index.Cap(ceiling, evicted);

	StoreState = STORE_READY;
	_StoreState = 1;

	if (!evicted.empty()) {
		Store_Drop(evicted);
		Store_Write();
	}

	return(StoreState != STORE_OFF);
#else
	return(false);
#endif
}


/// <summary>Notes blocks the index has let go of, so the batch deletes them.</summary>
void ISOHttpSourceClass::Store_Drop(std::vector<std::uint64_t> const & evicted)
{
	char key[32];

	for (std::uint64_t gone : evicted) {
		std::snprintf(key, sizeof(key), "%s%llu", Removals.empty() ? "" : ",",
			(unsigned long long)gone);
		Removals += key;
	}
}


/// <summary>Serves a span out of the store, if the store holds all of it.</summary>
/// <returns>bool; Was the whole span delivered without a request?</returns>
bool ISOHttpSourceClass::Store_Serve(std::uint64_t offset, void * buffer, unsigned int length,
	ReadType const & read)
{
#if defined(OPENTS_WASM_JSPI)
	if (!Store_Ready()) return(false);

	std::uint64_t const first = offset / (std::uint64_t)BLOCK_SIZE;
	std::uint64_t const last = (offset + length - 1) / (std::uint64_t)BLOCK_SIZE;

	for (std::uint64_t index = first; index <= last; index++) {
		if (!Index.Holds(index)) return(false);
	}

	double const stalled = emscripten_get_now();

	if (ISO_Store_Read(Slot.c_str(), (double)offset, buffer, length, (unsigned int)BLOCK_SIZE) != 1) {

		/*
		**	The record and the blocks are written together, so they cannot disagree by
		**	themselves. Another tab on the same origin can make them disagree, and there is
		**	nothing to be gained by arguing with it: the store is left alone for the rest of
		**	the run and the image is read off the server.
		*/
		StoreState = STORE_OFF;
		_StoreState = 2;
		return(false);
	}

	Account_For_Stall(Meter, read.Offset, read.Length, emscripten_get_now() - stalled, STALL_STORE);
	_StoreHits += (unsigned int)(last - first + 1);
	_StoreBytes += length;
	return(true);
#else
	return(false);
#endif
}


/// <summary>Stages every whole block a fetch delivered.</summary>
/// <remarks>Only a block the span covers entirely is stored, so what the store holds is
/// always a whole block; the partial blocks at the ends of a span are covered by the window
/// the read path puts them through.</remarks>
void ISOHttpSourceClass::Store_Keep(std::uint64_t offset, void const * buffer, unsigned int length,
	ISOBlockIndexClass::AdmitType how)
{
#if defined(OPENTS_WASM_JSPI)
	if (!Store_Ready() || StoreState != STORE_READY) return;

	std::uint64_t const stop = offset + length;
	std::uint64_t index = (offset + (std::uint64_t)BLOCK_SIZE - 1) / (std::uint64_t)BLOCK_SIZE;

	for (;;) {
		std::uint64_t const at = index * (std::uint64_t)BLOCK_SIZE;
		if (at >= Length || at >= stop) break;

		std::uint64_t size = Length - at;
		if (size > (std::uint64_t)BLOCK_SIZE) size = (std::uint64_t)BLOCK_SIZE;
		if (at + size > stop) break;

		/*
		**	A block the index will not take is not staged. The record is what says a block
		**	may be served, so one written without an entry describing it is bytes the origin
		**	is charged for and nothing ever reads back.
		*/
		bool const room = (how != ISOBlockIndexClass::ADMIT_GUESS) ||
			(Index.Bytes() + size <= Index.Cap());

		if (!Index.Holds(index) && room) {
			if (ISO_Store_Stage(Slot.c_str(), (double)index, (unsigned char const *)buffer + (at - offset),
					(unsigned int)size) == 1) {

				std::vector<std::uint64_t> evicted;

				Index.Note(index, size, evicted, how);
				Store_Drop(evicted);
				Staging.push_back(index);

				Staged++;
				StagedAt = emscripten_get_now();
				_StoreKept++;
			}
		}

		index++;
	}

	if (Staged >= (unsigned int)STORE_BATCH) Store_Write();
#endif
}


/// <summary>Writes the staged batch, its evictions and the record, in one transaction.</summary>
/// <remarks>
/// A refused write is told apart from a broken one. The origin refusing another block leaves
/// everything an earlier batch wrote exactly where it was, so only the blocks of this batch
/// are given up and the image goes on serving the rest; nothing more is offered to the store
/// for the rest of the run. A database that has stopped answering says nothing about what it
/// still holds, so that one is given up whole and the image is read off the server.
/// </remarks>
void ISOHttpSourceClass::Store_Write(void)
{
#if defined(OPENTS_WASM_JSPI)
	if (StoreState != STORE_READY) return;
	if (Staged == 0 && Removals.empty()) return;

	int const written = ISO_Store_Write(Slot.c_str(), Index.Encode().c_str(), Removals.c_str());

	Staged = 0;
	Removals.clear();

	if (written == 1) {
		Staging.clear();
		return;
	}

	if (written < 0) {
		Index.Forget(Staging);
		Staging.clear();
		StoreState = STORE_FULL;
		_StoreState = 3;
		return;
	}

	Index.Reset(Signature);
	Staging.clear();
	StoreState = STORE_OFF;
	_StoreState = 2;
#endif
}


/// <summary>Writes any open image's batch that has been left sitting.</summary>
void ISOHttpSourceClass::Store_Settle(void)
{
#if defined(OPENTS_WASM_JSPI)
	double const now = emscripten_get_now();

	for (ISOHttpSourceClass * source : _Open) {
		if (source->Staged > 0 && (now - source->StagedAt) > STORE_IDLE) source->Store_Write();
	}
#endif
}


void ISOHttpSourceClass::Store_Discard(void)
{
#if defined(OPENTS_WASM_JSPI)
	ISO_Store_Forget(Slot.c_str());
	Staged = 0;
	Removals.clear();
	Staging.clear();
#endif
}


bool ISOHttpSourceClass::Transfer(std::uint64_t offset, void * buffer, unsigned int length,
	ReadType const & read)
{
	unsigned char * cursor = (unsigned char *)buffer;
	unsigned int remaining = length;
	double const stalled = emscripten_get_now();

	Account_For_Transfer(Meter, offset, length);

	/*
	**	The engine is about to wait on this, so nothing asked for on a guess is left running
	**	in front of it. What that costs is the guess's, not the reading's.
	*/
#if defined(OPENTS_WASM_JSPI)
	double const given = ISO_Http_Ahead_Busy(Url.c_str(), 1);

	if (given > 0.0) _AheadWaste += (std::uint64_t)given;
#endif

	/*
	**	A server is entitled to answer a range with less than was asked for, so the request
	**	is repeated from where the answer stopped rather than reported short.
	*/
	while (remaining > 0) {

		/*
		**	Timed because this is where the link says what it costs. A read the engine is
		**	waiting on is one round trip and then its bytes, which is exactly the pair the
		**	window is sized from, and it is measured here rather than probed for because the
		**	engine is making the request either way.
		*/
		double const began = emscripten_get_now();
		int const got = ISO_Http_Transfer(Url.c_str(), (double)offset, cursor, remaining);

		if (got <= 0) {

			/*
			**	On an image nothing was probed for, a range the server will not answer is
			**	the first evidence that what the record said about it is wrong. Asking the
			**	server settles it, and a record that turns out to have described another
			**	file takes its stored blocks with it.
			*/
			if (Revive()) continue;

			return(false);
		}

		Link.Note((std::uint64_t)got, emscripten_get_now() - began);

		_LinkTrip = Link.Trip();
		_LinkRate = Link.Rate();
		_LinkWindow = Link.Window();

		/*
		**	And what the link is now believed to cost goes back into the record, so the next
		**	launch opens its window on a reading of this link rather than on the floor. It is
		**	held to one write in a while because the estimates move slowly and the store it
		**	goes to is a synchronous one.
		*/
		if (Link.Measured() && (emscripten_get_now() - Learned) > LEARN_IDLE) Learn();

		cursor += (unsigned int)got;
		offset += (unsigned int)got;
		remaining -= (unsigned int)got;
	}

	Account_For_Stall(Meter, read.Offset, read.Length, emscripten_get_now() - stalled, STALL_TRANSFER);
	return(true);
}


/// <summary>Delivers a span the store may already hold, and stores it when it does not.</summary>
/// <remarks>The span is a whole number of whole blocks, which is what makes it storable.</remarks>
bool ISOHttpSourceClass::Fetch_Run(std::uint64_t offset, void * buffer, unsigned int length,
	ReadType const & read)
{
	/*
	**	A span is fetched whole, so it can begin before the read that wanted it and end after
	**	it. What a stall in here is a wait for is the part of the span the read asked for,
	**	and that is what the record is given rather than the span.
	*/
	ReadType const part = read.Within(offset, length);

	/*
	**	The look-ahead comes first. What it holds was asked for because the run was heading
	**	here, so a span it covers is already paid for and asking the store about it would
	**	only add a wait to bytes that are on the heap.
	*/
	if (Ahead_Serve(offset, buffer, length, part)) {
		Store_Keep(offset, buffer, length, ISOBlockIndexClass::ADMIT_READ);
		Look_Ahead();
		return(true);
	}

	/*
	**	A span the store answered for cost no round trip, so there is no latency in front of
	**	the run for a window to hide and asking for one would be bandwidth spent on bytes the
	**	browser is already holding. Only a span the network had to carry opens the window.
	*/
	if (Store_Serve(offset, buffer, length, part)) return(true);

	/*
	**	Everything that could have answered without a request has been asked. What is left
	**	is a round trip, and a read that may decline takes none: the blocks it wanted are
	**	put in flight and the window is opened behind them, and the read comes back empty
	**	so that whatever asked for it can go on and try again once they have landed.
	*/
#if defined(OPENTS_WASM_JSPI)
	if (ISODeferredReadClass::Deferring()) {
		Ahead_Want(offset, length);
		_Deferred++;
		ISODeferredReadClass::Decline();
		return(false);
	}
#endif

	/*
	**	The window is opened before this read is paid for rather than after it. The request
	**	it starts then runs alongside the one this read is about to wait on instead of behind
	**	it, which is what a run read faster than the network answers needs: a movie opened in
	**	one frame reads its first blocks back to back, with no decoding in between for a round
	**	trip to hide in.
	**
	*/
	Look_Ahead();

	if (!Transfer(offset, buffer, length, part)) return(false);

	Store_Keep(offset, buffer, length, ISOBlockIndexClass::ADMIT_READ);
	return(true);
}


ISOHttpSourceClass::BlockType const * ISOHttpSourceClass::Block(std::uint64_t index,
	ReadType const & read)
{
	for (std::size_t position = 0; position < Cache.size(); position++) {
		if (Cache[position].Index != index) continue;

		if (position != 0) {
			std::rotate(Cache.begin(), Cache.begin() + (std::ptrdiff_t)position,
				Cache.begin() + (std::ptrdiff_t)position + 1);
		}
		return(&Cache.front());
	}

	std::uint64_t const at = index * (std::uint64_t)BLOCK_SIZE;
	if (at >= Length) return(nullptr);

	std::uint64_t available = Length - at;
	if (available > (std::uint64_t)BLOCK_SIZE) available = (std::uint64_t)BLOCK_SIZE;

	BlockType fetched;
	fetched.Index = index;
	fetched.Data.resize((std::size_t)available);

	if (!Fetch_Run(at, fetched.Data.data(), (unsigned int)available, read)) return(nullptr);

	if (Cache.size() >= (std::size_t)BLOCK_CACHE) Cache.pop_back();
	Cache.insert(Cache.begin(), std::move(fetched));
	return(&Cache.front());
}


bool ISOHttpSourceClass::Read_At(std::uint64_t offset, void * buffer, unsigned int length)
{
	if (buffer == nullptr) return(false);
	if (length == 0) return(true);
	if (Length == 0 || offset > Length || (std::uint64_t)length > Length - offset) return(false);

	ReadType const read = {offset, length};

	/*
	**	A read that may decline and whose bytes are already coming declines before anything
	**	else happens. A reader that carries on without them asks again as often as it is
	**	called, and every one of those asks would otherwise be followed as a run: the image
	**	would spend the whole wait aiming its window at bytes it has already asked for, and
	**	taking it off the reading that is actually going on.
	*/
#if defined(OPENTS_WASM_JSPI)
	if (ISODeferredReadClass::Deferring() && Ahead_Pending(offset, length)) {
		_Deferred++;
		ISODeferredReadClass::Decline();
		return(false);
	}
#endif

	/*
	**	A batch left over from a burst of loading is written once the loading stops, so that
	**	what a run fetched is on disc rather than waiting for a batch that never fills. Every
	**	open image is settled here, not just this one: a disc the game is finished with is
	**	read no more and would otherwise hold its last batch for the rest of the run.
	*/
	Store_Settle();

	/*
	**	What the drainer asked for while nothing was being read is counted here, since the
	**	drainer runs on a timer of its own and cannot reach the counting itself. It is part
	**	of what the discs cost the connection whether or not any of it is ever read.
	*/
#if defined(OPENTS_WASM_JSPI)
	Soon_Keep();

	unsigned int guesses = 0;
	double const guessed = ISO_Http_Idle_Spent(Url.c_str(), &guesses);

	if (guessed > 0.0) {
		_Requests += guesses;
		_Fetched += (std::uint64_t)guessed;
		_AheadRequests += guesses;
		_AheadBytes += (std::uint64_t)guessed;
		_SoonRequests += guesses;
		_SoonBytes += (std::uint64_t)guessed;
	}

#endif

	/*
	**	Where the reads are going is followed before any of this one is served, so that a run
	**	being taken over is seen as it happens: what was asked for in front of the run that
	**	was displaced is bytes nobody will read and is abandoned rather than paid for. The
	**	runs still being read along keep what was asked for in front of them.
	*/
	std::uint64_t lost = 0;
	std::uint64_t stop = 0;

	bool const displaced = Ahead.Note(offset / (std::uint64_t)BLOCK_SIZE,
		(offset + length - 1) / (std::uint64_t)BLOCK_SIZE, lost, stop);

	if (displaced) Ahead_Drop(lost, stop);

	unsigned char * cursor = (unsigned char *)buffer;
	unsigned int remaining = length;

	while (remaining > 0) {
		std::uint64_t const index = offset / (std::uint64_t)BLOCK_SIZE;
		std::size_t const within = (std::size_t)(offset - index * (std::uint64_t)BLOCK_SIZE);

		/*
		**	A run of whole blocks is asked for in one go, which is what keeps an extent to a
		**	single request. What is left at either end of it is shorter than a block and comes
		**	through the window, so every block is either fetched whole or not fetched at all --
		**	and only a whole block is worth storing.
		*/
		std::uint64_t const whole = Length / (std::uint64_t)BLOCK_SIZE;

		if (within == 0 && remaining >= (unsigned int)BLOCK_SIZE && index < whole) {
			std::uint64_t count = remaining / (unsigned int)BLOCK_SIZE;
			if (count > whole - index) count = whole - index;

			unsigned int const span = (unsigned int)(count * (std::uint64_t)BLOCK_SIZE);

			if (!Fetch_Run(offset, cursor, span, read)) return(false);

			cursor += span;
			offset += span;
			remaining -= span;
			continue;
		}

		BlockType const * const block = Block(index, read);
		if (block == nullptr || within >= block->Data.size()) return(false);

		std::size_t chunk = block->Data.size() - within;
		if (chunk > (std::size_t)remaining) chunk = (std::size_t)remaining;

		std::memcpy(cursor, block->Data.data() + within, chunk);
		cursor += chunk;
		offset += chunk;
		remaining -= (unsigned int)chunk;
	}

	return(true);
}


void ISO_Image_Locations(std::vector<std::string> & locations)
{
	static char named[2048];
	static bool asked = false;

	locations.clear();

	if (!asked) {
		asked = true;
		named[0] = '\0';
		ISO_Image_Location_Query(named, (int)sizeof(named));
	}

	std::string const list(named);
	std::size_t cursor = 0;

	while (cursor < list.size()) {
		std::size_t stop = list.find('\n', cursor);
		if (stop == std::string::npos) stop = list.size();

		if (stop > cursor) locations.push_back(list.substr(cursor, stop - cursor));
		cursor = stop + 1;
	}
}


std::unique_ptr<ISOBlockSourceClass> ISO_Open_Location(char const * location)
{
	if (location == nullptr || *location == '\0') return(nullptr);

	/*
	**	A scheme separates a URL from a path, and a path is what the node build and the
	**	tests hand over. Nothing else distinguishes the two.
	*/
	if (std::strstr(location, "://") != nullptr) {
		std::unique_ptr<ISOHttpSourceClass> source = std::make_unique<ISOHttpSourceClass>();

		if (!source->Open(location)) return(nullptr);
		return(source);
	}

	std::unique_ptr<ISOLocalFileSourceClass> source = std::make_unique<ISOLocalFileSourceClass>();

	if (!source->Open(location)) {

		/*
		**	A bare name in a page is a URL relative to the document, which is how an image
		**	beside the module is named. There is no local file to fall back on there.
		*/
		std::unique_ptr<ISOHttpSourceClass> remote = std::make_unique<ISOHttpSourceClass>();

		if (!remote->Open(location)) return(nullptr);
		return(remote);
	}

	return(source);
}

#endif
