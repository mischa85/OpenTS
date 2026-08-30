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
// The second thing this file has to solve is that a reload should not pay for the image
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
** The one request that establishes what is being read. It answers the image's length, and
** with it the two things that identify the image to a later run: the URL the page resolved
** the location to, and whatever the server calls this version of it.
*/
EM_JS(double, ISO_Http_Probe, (char const * url, char * identity, int identitysize, char * validator, int validatorsize), {
	var write = function (text, buffer, size) {
		var count = 0;
		while (count < text.length && count + 1 < size) {
			var code = text.charCodeAt(count);
			HEAPU8[buffer + count] = (code > 126 || code < 32) ? 63 : code;
			count++;
		}
		HEAPU8[buffer + count] = 0;
	};

	write("", identity, identitysize);
	write("", validator, validatorsize);

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

		try {
			write(new URL(request.responseURL || UTF8ToString(url), location.href).href, identity, identitysize);
		} catch (error) {
			write(UTF8ToString(url), identity, identitysize);
		}

		var tag = request.getResponseHeader("ETag") || request.getResponseHeader("Last-Modified") || "";
		write(tag, validator, validatorsize);

		return parseFloat(total);
	} catch (error) {
		return -1;
	}
});


/*
** Where the image is. The page is asked first so that a harness can point the module at
** one of several images without rebuilding it, and the environment second so that the same
** module answers to a shell running it under node.
*/
EM_JS(int, ISO_Image_Location_Query, (char * buffer, int size), {
	var text = "";

	if (typeof Module !== "undefined" && Module["opentsImage"]) {
		text = "" + Module["opentsImage"];
	} else if (typeof process !== "undefined" && process.env && process.env["OPENTS_IMAGE"]) {
		text = "" + process.env["OPENTS_IMAGE"];
	} else if (typeof document !== "undefined") {
		text = "opents-data.iso";
	}

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
** Opens the database and hands back the record describing what it holds. A database that
** cannot be opened -- a private window, storage the user has turned off -- is not an error
** the run has to care about, so it is reported as simply having nothing.
*/
EM_ASYNC_JS(int, ISO_Store_Open, (char * record, int size), {
	var state = globalThis.__opentsIsoStore;
	if (state === undefined || state === null) return 0;

	try {
		state.db = await new Promise(function (resolve, reject) {
			var request = indexedDB.open("opents-iso", 1);
			request.onupgradeneeded = function () {
				var db = request.result;
				if (!db.objectStoreNames.contains("blocks")) db.createObjectStore("blocks");
				if (!db.objectStoreNames.contains("meta")) db.createObjectStore("meta");
			};
			request.onsuccess = function () { resolve(request.result); };
			request.onerror = function () { reject(request.error); };
			request.onblocked = function () { reject(new Error("blocked")); };
		});

		var text = await new Promise(function (resolve, reject) {
			var transaction = state.db.transaction("meta", "readonly");
			var get = transaction.objectStore("meta").get("record");
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
** Reads a span out of the store. The whole span is done in one transaction, so an extent
** the game reads in a single call costs one wait rather than one per block, and a block the
** current batch has staged but not yet written is served from the batch.
*/
EM_ASYNC_JS(int, ISO_Store_Read, (double offset, void * buffer, unsigned int length, unsigned int blocksize), {
	var state = globalThis.__opentsIsoStore;
	if (state === undefined || state === null || !state.usable || state.db === null) return 0;

	try {
		var first = Math.floor(offset / blocksize);
		var last = Math.floor((offset + length - 1) / blocksize);
		var wanted = [];
		var index = 0;

		for (index = first; index <= last; index++) {
			if (!state.staged.has(index)) wanted.push(index);
		}

		var found = new Map();

		if (wanted.length > 0) {
			await new Promise(function (resolve, reject) {
				var transaction = state.db.transaction("blocks", "readonly");
				var blocks = transaction.objectStore("blocks");
				wanted.forEach(function (key) {
					var get = blocks.get(key);
					get.onsuccess = function () { if (get.result) found.set(key, get.result); };
				});
				transaction.oncomplete = function () { resolve(0); };
				transaction.onerror = function () { reject(transaction.error); };
				transaction.onabort = function () { reject(transaction.error); };
			});
		}

		var written = 0;

		for (index = first; index <= last; index++) {
			var data = state.staged.has(index) ? state.staged.get(index) : found.get(index);
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
EM_JS(int, ISO_Store_Stage, (double index, void const * buffer, unsigned int length), {
	var state = globalThis.__opentsIsoStore;
	if (state === undefined || state === null || !state.usable || state.db === null) return 0;

	try {
		state.staged.set(index, HEAPU8.slice(buffer, buffer + length));
		return 1;
	} catch (error) {
		return 0;
	}
});


/*
** Writes the batch, the evictions it forced and the record that describes the result, all in
** the one transaction that makes them agree. Running out of quota is an ordinary outcome
** here: the store is given up for the rest of the run and the run carries on off the network.
*/
EM_ASYNC_JS(int, ISO_Store_Write, (char const * record, char const * removals), {
	var state = globalThis.__opentsIsoStore;
	if (state === undefined || state === null || !state.usable || state.db === null) return 0;

	var text = UTF8ToString(record);
	var drop = UTF8ToString(removals);

	try {
		await new Promise(function (resolve, reject) {
			var transaction = state.db.transaction(Array("blocks", "meta"), "readwrite");
			var blocks = transaction.objectStore("blocks");

			if (drop === "*") {
				blocks.clear();
			} else if (drop.length > 0) {
				drop.split(",").forEach(function (key) {
					var index = parseFloat(key);
					if (!state.staged.has(index)) blocks.delete(index);
				});
			}

			state.staged.forEach(function (data, index) { blocks.put(data, index); });
			transaction.objectStore("meta").put(text, "record");

			transaction.oncomplete = function () { resolve(0); };
			transaction.onerror = function () { reject(transaction.error); };
			transaction.onabort = function () { reject(transaction.error); };
		});

		state.staged.clear();
		return 1;
	} catch (error) {
		state.staged.clear();
		state.usable = false;
		return ((error && error.name) === "QuotaExceededError") ? -1 : 0;
	}
});

// Lets go of a batch that will not be written, which is the only thing teardown may do.
EM_JS(void, ISO_Store_Forget, (void), {
	var state = globalThis.__opentsIsoStore;
	if (state !== undefined && state !== null && state.staged) state.staged.clear();
});

#endif	// OPENTS_WASM_JSPI


static char const ISO_STORE_MAGIC[] = "opents-iso-1";


ISOBlockIndexClass::ISOBlockIndexClass(void) :
	Total(0)
{
}


/// <summary>Builds the key that says which image a stored block belongs to.</summary>
/// <remarks>
/// The length alone would let an image of the same size be read as this one, and a run that
/// serves one image's sectors as another's corrupts game data in a way that looks like
/// anything but a cache. So whatever the server will say about the version -- an entity tag,
/// a modification date -- is part of the key, and a server that says nothing leaves the key
/// resting on the URL and the length, which is the weakest form this takes.
/// </remarks>
std::string ISOBlockIndexClass::Signature(char const * url, std::uint64_t length, char const * validator)
{
	if (url == nullptr || *url == '\0' || length == 0) return(std::string());

	std::string key(url);

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
/// </remarks>
void ISOBlockIndexClass::Note(std::uint64_t index, std::uint64_t size, std::vector<std::uint64_t> & evicted)
{
	if (size == 0 || Held.count(index) != 0) return;

	while (!Order.empty() && Total + size > STORE_LIMIT) {
		EntryType const oldest = Order.front();

		Order.erase(Order.begin());
		Held.erase(oldest.Index);
		Total -= oldest.Size;
		evicted.push_back(oldest.Index);
	}

	if (Total + size > STORE_LIMIT) return;

	EntryType entry;
	entry.Index = index;
	entry.Size = size;
	Order.push_back(entry);
	Held.insert(index);
	Total += size;
}


ISOHttpSourceClass::ISOHttpSourceClass(void) :
	Length(0),
	StoreState(STORE_UNTRIED),
	Staged(0),
	StagedAt(0.0)
{
}


ISOHttpSourceClass::~ISOHttpSourceClass(void)
{
	Close();
}


/// <summary>Opens an image served from a URL.</summary>
/// <param name="url">Where the image is, absolute or relative to the page.</param>
/// <returns>bool; Did the server answer a ranged request and report the image length?</returns>
bool ISOHttpSourceClass::Open(char const * url)
{
	Close();

	if (url == nullptr || *url == '\0') return(false);

	Url = url;

	std::vector<char> identity(ISOBlockIndexClass::SIGNATURE_MAX);
	std::vector<char> validator(ISOBlockIndexClass::SIGNATURE_MAX);

	double const length = ISO_Http_Probe(Url.c_str(), identity.data(), (int)identity.size(),
		validator.data(), (int)validator.size());

	if (!(length > 0.0)) {
		Url.clear();
		return(false);
	}

	Length = (std::uint64_t)length;

	/*
	**	The store is not opened here. Open is reached from the first file the host cannot
	**	answer for, which the engine asks about while its static objects are still being
	**	constructed, and a wait there is not yet legal. What the probe learned is kept until
	**	a read arrives at a point where one is.
	*/
	Signature = ISOBlockIndexClass::Signature(
		(identity[0] != '\0') ? identity.data() : Url.c_str(), Length, validator.data());

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

	Url.clear();
	Signature.clear();
	Removals.clear();
	Length = 0;
	Cache.clear();
	Index.Reset(std::string());
	StoreState = STORE_UNTRIED;
	Staged = 0;
}


/*
**	What the images have cost so far, across every source. Requests counts round trips and
**	Fetched the bytes they carried; Touched marks every block-sized window any request has
**	covered, so its population is the working set -- the part of the image the game turns
**	out to need, which is the figure that says whether reading on demand was worth it.
*/
static unsigned int _Requests = 0;
static std::uint64_t _Fetched = 0;
static std::vector<bool> _Touched;

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


static void Account_For_Transfer(std::uint64_t offset, unsigned int length)
{
	_Requests++;
	_Fetched += length;

	std::uint64_t const first = offset / ISOHttpSourceClass::BLOCK_SIZE;
	std::uint64_t const last = (offset + length - 1) / ISOHttpSourceClass::BLOCK_SIZE;

	if (_Touched.size() <= last) {
		_Touched.resize((std::size_t)last + 1, false);
	}

	for (std::uint64_t index = first; index <= last; index++) {
		_Touched[(std::size_t)index] = true;
	}
}


extern "C" {

EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Requests(void) {return(_Requests);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Fetched(void) {return((double)_Fetched);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Block_Size(void) {return(ISOHttpSourceClass::BLOCK_SIZE);}

EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Unique_Blocks(void)
{
	unsigned int count = 0;

	for (bool touched : _Touched) {
		if (touched) count++;
	}

	return(count);
}

/*
** 0 the store was never reached, 1 it is serving, 2 it was given up. A run that reports 0
** with the scaffold built in never got as far as a read underneath main.
*/
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Store_State(void) {return(_StoreState);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Store_Hits(void) {return(_StoreHits);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Store_Bytes(void) {return((double)_StoreBytes);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Store_Kept(void) {return(_StoreKept);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Store_Discarded(void) {return(_StoreDiscarded);}

}


/*
**	------------------------------------------------------------------------------------
**	The store, as the source uses it. Without the suspension scaffold none of it can be
**	reached, and the source reads exactly as it did before.
**	------------------------------------------------------------------------------------
*/

/// <summary>Is the store open and serving this image?</summary>
/// <remarks>The first call that finds a wait legal opens the database and reads the record
/// describing what it holds. A record written for another image is not evidence about this
/// one, so the blocks it describes are cleared rather than trusted; nothing else in the
/// engine can tell the difference between a stale sector and a real one.</remarks>
bool ISOHttpSourceClass::Store_Ready(void)
{
#if defined(OPENTS_WASM_JSPI)
	if (StoreState == STORE_READY) return(true);
	if (StoreState == STORE_OFF) return(false);

	if (Signature.empty()) {
		StoreState = STORE_OFF;
		_StoreState = 2;
		return(false);
	}

	if (ISO_Store_Under_Main() == 0) return(false);

	std::vector<char> record(ISOBlockIndexClass::RECORD_MAX);
	record[0] = '\0';

	if (ISO_Store_Open(record.data(), (int)record.size()) != 1) {
		StoreState = STORE_OFF;
		_StoreState = 2;
		return(false);
	}

	if (!Index.Adopt(record.data(), Signature)) {
		_StoreDiscarded++;

		if (ISO_Store_Write(Index.Encode().c_str(), "*") != 1) {
			StoreState = STORE_OFF;
			_StoreState = 2;
			return(false);
		}
	}

	StoreState = STORE_READY;
	_StoreState = 1;
	return(true);
#else
	return(false);
#endif
}


/// <summary>Serves a span out of the store, if the store holds all of it.</summary>
/// <returns>bool; Was the whole span delivered without a request?</returns>
bool ISOHttpSourceClass::Store_Serve(std::uint64_t offset, void * buffer, unsigned int length)
{
#if defined(OPENTS_WASM_JSPI)
	if (!Store_Ready()) return(false);

	std::uint64_t const first = offset / (std::uint64_t)BLOCK_SIZE;
	std::uint64_t const last = (offset + length - 1) / (std::uint64_t)BLOCK_SIZE;

	for (std::uint64_t index = first; index <= last; index++) {
		if (!Index.Holds(index)) return(false);
	}

	if (ISO_Store_Read((double)offset, buffer, length, (unsigned int)BLOCK_SIZE) != 1) {

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
void ISOHttpSourceClass::Store_Keep(std::uint64_t offset, void const * buffer, unsigned int length)
{
#if defined(OPENTS_WASM_JSPI)
	if (!Store_Ready()) return;

	std::uint64_t const stop = offset + length;
	std::uint64_t index = (offset + (std::uint64_t)BLOCK_SIZE - 1) / (std::uint64_t)BLOCK_SIZE;

	for (;;) {
		std::uint64_t const at = index * (std::uint64_t)BLOCK_SIZE;
		if (at >= Length || at >= stop) break;

		std::uint64_t size = Length - at;
		if (size > (std::uint64_t)BLOCK_SIZE) size = (std::uint64_t)BLOCK_SIZE;
		if (at + size > stop) break;

		if (!Index.Holds(index)) {
			if (ISO_Store_Stage((double)index, (unsigned char const *)buffer + (at - offset),
					(unsigned int)size) == 1) {

				std::vector<std::uint64_t> evicted;
				Index.Note(index, size, evicted);

				char key[32];
				for (std::uint64_t gone : evicted) {
					std::snprintf(key, sizeof(key), "%s%llu", Removals.empty() ? "" : ",",
						(unsigned long long)gone);
					Removals += key;
				}

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
void ISOHttpSourceClass::Store_Write(void)
{
#if defined(OPENTS_WASM_JSPI)
	if (StoreState != STORE_READY) return;
	if (Staged == 0 && Removals.empty()) return;

	int const written = ISO_Store_Write(Index.Encode().c_str(), Removals.c_str());

	Staged = 0;
	Removals.clear();

	if (written != 1) {

		/*
		**	Out of quota, or a database that has stopped answering. Neither is the run's
		**	problem: the store is given up, the index with it, and the image goes on being
		**	read off the server.
		*/
		Index.Reset(Signature);
		StoreState = STORE_OFF;
		_StoreState = 2;
	}
#endif
}


void ISOHttpSourceClass::Store_Discard(void)
{
#if defined(OPENTS_WASM_JSPI)
	ISO_Store_Forget();
	Staged = 0;
	Removals.clear();
#endif
}


bool ISOHttpSourceClass::Transfer(std::uint64_t offset, void * buffer, unsigned int length)
{
	unsigned char * cursor = (unsigned char *)buffer;
	unsigned int remaining = length;

	Account_For_Transfer(offset, length);

	/*
	**	A server is entitled to answer a range with less than was asked for, so the request
	**	is repeated from where the answer stopped rather than reported short.
	*/
	while (remaining > 0) {
		int const got = ISO_Http_Transfer(Url.c_str(), (double)offset, cursor, remaining);

		if (got <= 0) return(false);

		cursor += (unsigned int)got;
		offset += (unsigned int)got;
		remaining -= (unsigned int)got;
	}

	return(true);
}


/// <summary>Delivers a span the store may already hold, and stores it when it does not.</summary>
/// <remarks>The span is a whole number of whole blocks, which is what makes it storable.</remarks>
bool ISOHttpSourceClass::Fetch_Run(std::uint64_t offset, void * buffer, unsigned int length)
{
	if (Store_Serve(offset, buffer, length)) return(true);
	if (!Transfer(offset, buffer, length)) return(false);

	Store_Keep(offset, buffer, length);
	return(true);
}


ISOHttpSourceClass::BlockType const * ISOHttpSourceClass::Block(std::uint64_t index)
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

	if (!Fetch_Run(at, fetched.Data.data(), (unsigned int)available)) return(nullptr);

	if (Cache.size() >= (std::size_t)BLOCK_CACHE) Cache.pop_back();
	Cache.insert(Cache.begin(), std::move(fetched));
	return(&Cache.front());
}


bool ISOHttpSourceClass::Read_At(std::uint64_t offset, void * buffer, unsigned int length)
{
	if (buffer == nullptr) return(false);
	if (length == 0) return(true);
	if (Length == 0 || offset > Length || (std::uint64_t)length > Length - offset) return(false);

	/*
	**	A batch left over from a burst of loading is written once the loading stops, so that
	**	what a run fetched is on disc rather than waiting for a batch that never fills.
	*/
	if (Staged > 0 && (emscripten_get_now() - StagedAt) > STORE_IDLE) Store_Write();

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

			if (!Fetch_Run(offset, cursor, span)) return(false);

			cursor += span;
			offset += span;
			remaining -= span;
			continue;
		}

		BlockType const * const block = Block(index);
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


char const * ISO_Image_Location(void)
{
	static char location[512];
	static bool asked = false;

	if (!asked) {
		asked = true;
		location[0] = '\0';
		ISO_Image_Location_Query(location, (int)sizeof(location));
	}

	return(location);
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
