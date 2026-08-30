/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Where a browser build gets its game data. A page has no filesystem to run out of, so the
// disc images are left on a web server and read with range requests. iso9660.h asks a block
// source for nothing but a synchronous read at an absolute offset, which is exactly what one
// ranged GET answers, so the parser is the same one a local image goes through.
//
// A game is spread over more than one disc, so what is named is a list of images and each
// one gets a source of its own. Everything below is per image: what a run has fetched, what
// the browser's database is holding, and the key the two are found under.

#pragma once

#include "iso9660.h"

#if defined(__EMSCRIPTEN__)

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>


/*
** The unit an image is fetched, cached and stored in. Everything below that counts blocks
** counts these: a read too short to be worth its own request is served from one, a run's
** window is measured in them, and the store holds nothing else.
*/
enum {
	ISO_BLOCK_SIZE = 65536
};


/*
 * What the browser's database is holding for one image, kept in memory while the run lasts
 * and written back beside the blocks so the next run knows what is there without reading
 * it. The whole of it is ordinary arithmetic over a signature and a list of block numbers,
 * which is what lets the part that decides whether a stored block may be believed be tested
 * without a server, a document or a database.
 *
 * Keeping the record beside the blocks in one transaction is what makes the two agree: a
 * transaction that does not complete leaves neither the blocks nor the record it wrote.
 */
class ISOBlockIndexClass
{
	public:

		/*
		** What a block being offered to the store is worth, which is what decides whether it
		** may push one already held out. A block the engine read is the working set: the
		** next run reads it again, so it displaces the oldest of what is there. A block
		** nobody has read is a guess that has not come good yet, and a guess is not worth
		** anything already proved, so a full store declines it instead.
		*/
		enum AdmitType {
			ADMIT_READ,
			ADMIT_GUESS
		};

		ISOBlockIndexClass(void);

		/// <summary>Builds the key that says which image a stored block belongs to.</summary>
		/// <param name="location">Where the image was asked for, made absolute. Never the URL
		/// a redirect ended at, which names a node rather than an image.</param>
		/// <param name="length">The image's length in bytes.</param>
		/// <param name="validator">What the server calls this version of it -- an entity tag
		/// or a modification date -- or an empty string when it names none.</param>
		/// <returns>The key, or an empty string when the image cannot be identified.</returns>
		static std::string Signature(char const * location, std::uint64_t length, char const * validator);

		/// <summary>Builds the key the store holds one image's blocks and record under.</summary>
		/// <param name="location">Where the image was asked for, made absolute.</param>
		/// <returns>The key, or an empty string when the image cannot be identified.</returns>
		static std::string Store_Slot(char const * location);

		void Reset(std::string const & signature);

		/// <summary>Takes on a stored record, if it was written for this image.</summary>
		/// <param name="record">What the database held, or an empty string for a fresh one.</param>
		/// <param name="signature">The key the current image answers to.</param>
		/// <returns>bool; May the blocks the record lists be served? A false leaves the index
		/// empty and means the stored blocks belong to something else and must go.</returns>
		bool Adopt(char const * record, std::string const & signature);

		std::string Encode(void) const;

		bool Holds(std::uint64_t index) const {return(Held.count(index) != 0);}

		/// <summary>Records a block as stored, evicting whatever no longer fits.</summary>
		/// <param name="index">The block's number.</param>
		/// <param name="size">The bytes it holds.</param>
		/// <param name="evicted">Filled in with the blocks that must be deleted to make room.</param>
		/// <param name="how">Whether the block may displace one already held.</param>
		void Note(std::uint64_t index, std::uint64_t size, std::vector<std::uint64_t> & evicted,
			AdmitType how = ADMIT_READ);

		/// <summary>Stops serving blocks a write turned out not to have stored.</summary>
		/// <param name="indices">The blocks the refused batch was carrying.</param>
		void Forget(std::vector<std::uint64_t> const & indices);

		/// <summary>Sets how much of this image may be kept.</summary>
		/// <param name="bytes">The ceiling, or zero to leave the one already set.</param>
		/// <remarks>Whatever no longer fits is dropped from the index at once, so lowering
		/// the ceiling is answered by the same eviction a full store is.</remarks>
		void Cap(std::uint64_t bytes, std::vector<std::uint64_t> & evicted);

		std::uint64_t Cap(void) const {return(Ceiling);}

		std::uint64_t Bytes(void) const {return(Total);}
		std::size_t Count(void) const {return(Order.size());}
		std::string const & Key(void) const {return(Sig);}

		/*
		** How much of an image may be kept when the browser will not say how much the origin
		** is allowed. The working set of a mission is a few tens of megabytes, so this holds
		** one comfortably while staying far enough under any plausible quota that the store
		** is not the reason a saved game will not fit.
		*/
		static constexpr std::uint64_t STORE_LIMIT = 64ull * 1024ull * 1024ull;

		/*
		** And the most one may be given when it does say. A disc's data archives come to
		** something over a hundred and fifty megabytes at the largest, so this holds a whole
		** disc's worth with room to spare; past that the discs would be taking a share of a
		** large quota that nothing about them earns.
		*/
		static constexpr std::uint64_t STORE_MAX = 256ull * 1024ull * 1024ull;

		/*
		** And what share of the origin's allowance one image may take. A set is a handful of
		** discs, so an eighth apiece leaves the origin most of what it is allowed for saved
		** games and for whatever else the page keeps -- which matters only where the quota
		** is small, since the ceiling above is what binds on a machine with room.
		*/
		static constexpr double STORE_SHARE = 0.125;

		// A key long enough for any URL a page will resolve, and a record long enough for the
		// key and for the block list a full store holds: four thousand blocks fill the
		// ceiling above and no entry describing one runs past twenty characters.
		static constexpr std::size_t SIGNATURE_MAX = 512;
		static constexpr std::size_t RECORD_MAX = 262144;

	private:

		struct EntryType {
			std::uint64_t Index;
			std::uint64_t Size;
		};

		std::string Sig;
		std::uint64_t Total;
		std::uint64_t Ceiling;
		std::vector<EntryType> Order;
		std::unordered_set<std::uint64_t> Held;
};


/*
 * What the link to one image turns out to cost, measured from the requests the engine is
 * already making rather than from any traffic of its own.
 *
 * A request costs a round trip before its first byte and then the bytes themselves, and the
 * two have to be told apart because they scale differently: the trip is paid once however
 * much is asked for, so the further away a server is the more of the image one request
 * should carry and the further in front of the reading it should run. A disc on the same
 * machine and a disc on the far side of the world are the same code path with the two
 * numbers three orders of magnitude apart, which is why nothing here is a constant.
 *
 * The two are separated by what a request is asked for. A directory sector is two kilobytes
 * and its whole cost is the trip; a block or a span is large enough that what is left once
 * the trip is taken off is the rate. Neither estimate is allowed to settle on one sample:
 * both follow the readings, quickly towards a better one and slowly towards a worse, so an
 * early outlier is left behind within a few requests rather than steering the run.
 *
 * Nothing here fetches or waits. It is arithmetic over pairs of bytes and milliseconds, and
 * is tested as such.
 */
class ISOLinkClass
{
	public:
		ISOLinkClass(void);

		void Reset(void);

		/// <summary>Takes in what one completed request cost.</summary>
		/// <param name="bytes">How many bytes it carried.</param>
		/// <param name="milliseconds">How long it took, start to finish.</param>
		void Note(std::uint64_t bytes, double milliseconds);

		double Trip(void) const {return(Round);}
		double Rate(void) const {return(Speed);}
		bool Measured(void) const {return(Round > 0.0 && Speed > 0.0);}

		/// <summary>How many blocks a run keeps in front of itself.</summary>
		/// <remarks>The bandwidth-delay product, doubled. A refill is asked for once the
		/// window is half spent, so the half that is left has to carry the reading for the
		/// round trip the refill costs; at most that half is drained at the rate the link
		/// delivers, which makes one round trip's worth of bytes the half and two the
		/// whole. A link with nothing between it and the disc lands on the floor, which is
		/// what a local file and a server on the same machine both get.</remarks>
		unsigned int Window(void) const;

		/// <summary>How many blocks one request asks for.</summary>
		/// <remarks>The window in a fixed number of requests, so a distant link asks for
		/// more at a time rather than more often: splitting a window into requests buys
		/// nothing once the trip dominates what each of them costs.</remarks>
		unsigned int Span(void) const;

		/// <summary>How many requests one image may have outstanding.</summary>
		/// <remarks>The span scales with the window, so the count needed to keep the link
		/// full barely moves and stays at the floor for anything close by. What does move
		/// is what a single stream can carry: a connection to a distant server spends its
		/// first round trips opening its congestion window, so a long trip is allowed more
		/// requests beside each other.</remarks>
		unsigned int Flights(void) const;

		/// <summary>How many bytes are worth taking rather than paying another trip for.</summary>
		/// <remarks>One round trip's worth of them. Reaching further than this costs more
		/// than asking again would have, which is the whole of why a small file is worth
		/// fetching whole and a large one is not.</remarks>
		std::uint64_t Reach(void) const;

		enum {
			WINDOW_MIN = 2,		// Blocks in front of a run whatever the link costs.
			WINDOW_MAX = 128,	// And the most, which bounds one run's reach at eight megabytes.
			SPAN_MIN = 8,		// Blocks per request, so the first of them lands early.
			SPAN_MAX = 32,		// And the most, which is one request for two megabytes.
			SPLIT = 4,			// Requests a full window is asked for in.
			FLIGHTS_MIN = 4,	// Requests outstanding per image.
			FLIGHTS_MAX = 8
		};

		// A request no larger than this is all round trip; one at least this large has a
		// rate in it worth reading. Between them a request says nothing either way.
		static constexpr std::uint64_t TRIP_MAX = 8192;
		static constexpr std::uint64_t RATE_MIN = 32768;

		// How far one reading moves an estimate, towards a smaller and towards a larger.
		// Falling fast finds the floor a link is capable of; rising slowly keeps one queued
		// request from widening every window behind it.
		static constexpr double FALL = 0.34;
		static constexpr double RISE = 0.10;

		// And how far out of step with what is believed one reading may be before it is
		// treated as a property of that moment rather than of the link.
		static constexpr double SURGE = 4.0;

		// Round trips a window covers, and how long a trip has to be before one request at
		// a time stops filling the link, in milliseconds.
		static constexpr double COVER = 2.0;
		static constexpr double CROWD = 60.0;

	private:

		static double Follow(double current, double sample);

		double Round;
		double Speed;
};


/*
 * Where a run of reads is heading, and how far in front of it the fetching may go.
 *
 * A movie is read a frame at a time while it plays, and a read that leaves the block the
 * last one ended in costs a round trip the decoding cannot overlap, because the transport
 * does not return until the bytes are there. Watching the cursor is what lets the span in
 * front of it be asked for early instead, so the round trip is spent while the frames
 * behind it are still being decoded.
 *
 * A run is followed either because it was declared or because it was noticed. The file layer
 * declares one when it opens a file: an ISO9660 file is a run of consecutive sectors with a
 * known start and a known end, so a read of it is known to be sequential from its first byte
 * and known to stop where the file does. A run nobody declared has to be noticed instead,
 * which costs the two forward reads it takes to believe it and leaves the far end unknown.
 *
 * Either way the window earns its size rather than starting at it: a run may reach no
 * further in front of the reading than the reading has already covered, so a burst that
 * stops has over-read by at most what it read. Where a run was declared it also stops at
 * the end of the file, which is what makes reaching a long way in front of a movie cost
 * nothing when the movie is watched to the end.
 *
 * Nothing here fetches. It decides which blocks are worth asking for and how far the asking
 * may run, which is arithmetic over block numbers and is tested as such.
 */
class ISOReadAheadClass
{
	public:
		ISOReadAheadClass(void);

		void Reset(void);

		/// <summary>Takes on a run the file layer has declared.</summary>
		/// <param name="first">The first block of the run.</param>
		/// <param name="stop">The block it ends before.</param>
		/// <remarks>The run is believed at once, since what declared it knows the reading
		/// is sequential; it has covered nothing yet, so the window it may open is still
		/// the smallest one.</remarks>
		void Begin(std::uint64_t first, std::uint64_t stop);

		/// <summary>Has this run been declared, and does it end where one says?</summary>
		bool Bounded(void) const {return(Stop != 0);}
		std::uint64_t Limit(void) const {return(Stop);}

		/// <summary>Would a read carry on where this run has reached?</summary>
		/// <param name="first">The first block the read touches.</param>
		/// <returns>bool; A run nobody has read continues nothing.</returns>
		/// <remarks>A read that starts in the block the run has just been in, or in the one
		/// after it, continues it: a movie's frames are far shorter than a block, so most
		/// reads do not leave the block at all and only the ones that do count as progress.</remarks>
		bool Continues(std::uint64_t first) const;

		/// <summary>Follows a read to the blocks it covered.</summary>
		/// <param name="first">The first block the read touched.</param>
		/// <param name="last">The last block it touched.</param>
		/// <remarks>A read the run does not continue begins it again where that read landed,
		/// since what was asked for in front of the old cursor is bytes nobody wants.</remarks>
		void Note(std::uint64_t first, std::uint64_t last);

		/// <summary>Reports the span in front of the cursor worth asking for now.</summary>
		/// <param name="blocks">How many blocks the image holds.</param>
		/// <param name="window">The most the link is worth reaching in front of a run.</param>
		/// <param name="span">The most one request is worth asking for.</param>
		/// <param name="start">Receives the first block of the span.</param>
		/// <param name="count">Receives how many blocks it covers.</param>
		/// <returns>bool; Is there a span worth asking for?</returns>
		bool Span(std::uint64_t blocks, unsigned int window, unsigned int span,
			std::uint64_t & start, std::uint64_t & count) const;

		/// <summary>Records that every block below one has been asked for or found.</summary>
		void Issued(std::uint64_t upto);

		unsigned int Run(void) const {return(Length);}
		std::uint64_t Cursor(void) const {return(Next);}
		std::uint64_t Edge(void) const {return(Filled);}

		enum {
			RUN_MIN = 2,		// Reads in a forward run before an undeclared one is believed.

			/*
			** And how far a declared run reaches whatever the link is measured to be worth.
			** A declared run is not a guess -- the file layer says the reading is forward and
			** says where it stops -- so the floor a guess is held to does not apply to it,
			** and on a distant link the floor is the whole problem: two blocks is smaller
			** than one of a movie player's own top-up reads, so a window sitting on the floor
			** can never get in front of the reading and every top-up is a round trip. Four
			** megabytes covers several trips on any link slow enough to need covering.
			*/
			BOUND_MIN = 64,

			// And no less than this many times the last read, so a reader taking large
			// bites is still fetched further ahead than it can consume in one.
			BOUND_READS = 4
		};

	private:

		std::uint64_t Next;
		std::uint64_t Filled;
		std::uint64_t Wide;
		std::uint64_t From;
		std::uint64_t Stop;
		unsigned int Length;
};


/*
 * The runs an image is being read along at once.
 *
 * One cursor is enough for a movie the engine plays with nothing else going on, and that is
 * how a briefing is read. A clip that plays in the sidebar is not: a mission reads its map,
 * its artwork, its speech and its music off the same image while the clip streams, and every
 * one of those reads lands somewhere else. With a single cursor each of them is a seek that
 * ends the movie's run and abandons what was asked for in front of it, so the run never
 * lasts long enough to earn a window and the clip pays a round trip for every block.
 *
 * Following a few runs at once is what keeps them out of each other's way. A read joins the
 * run it carries on; a read that carries on none takes over the run that has gone longest
 * without one, and only that run's outstanding span is given up. The set is small and fixed,
 * so what is being followed is bounded and so is what may be in flight for it.
 */
class ISOReadRunsClass
{
	public:
		ISOReadRunsClass(void);

		void Reset(void);

		/// <summary>Follows a read to the run it belongs to, which becomes the current one.</summary>
		/// <param name="first">The first block the read touched.</param>
		/// <param name="last">The last block it touched.</param>
		/// <param name="lost">Receives the first block of a displaced run's outstanding span.</param>
		/// <param name="stop">Receives the block that span ends before.</param>
		/// <returns>bool; Did the read displace a run that had a span outstanding? What was
		/// asked for in front of that run is bytes nobody will read now.</returns>
		bool Note(std::uint64_t first, std::uint64_t last, std::uint64_t & lost, std::uint64_t & stop);

		/// <summary>Takes in a run of blocks the file layer says is one file.</summary>
		/// <param name="first">The first block of the file.</param>
		/// <param name="stop">The block it ends before.</param>
		/// <remarks>A declaration is only ever remembered here, never acted on. The engine
		/// declares a file whenever it opens one and opens far more of them than it reads,
		/// so a declaration that took a run over would spend most of its time throwing away
		/// what a stream still being read had asked for. The reading is what decides: a read
		/// that starts a run inside a declared file takes that file's end with it.</remarks>
		void Declare(std::uint64_t first, std::uint64_t stop);

		ISOReadAheadClass & Current(void) {return(Runs[Order[0]]);}
		ISOReadAheadClass const & Current(void) const {return(Runs[Order[0]]);}

		/*
		** How many runs are followed. A clip streaming while a mission reads its map, its
		** artwork and its music is four, which is what this is sized for; a fifth stream
		** takes the oldest of them over rather than growing the set.
		*/
		enum {
			RUNS = 4,

			/*
			** And how many declared files are remembered while they wait for a read to
			** arrive in one of them. The engine opens a handful at a time -- an archive,
			** the file inside it, the artwork and the audio beside it -- and the oldest
			** goes when a fifth is declared.
			*/
			BOUNDS = 8
		};

	private:

		struct BoundType {
			std::uint64_t First;
			std::uint64_t Stop;
		};

		/// <summary>Finds the declared file a read has landed in.</summary>
		/// <returns>The block that file ends before, or zero when no declaration covers it.</returns>
		std::uint64_t Bound(std::uint64_t first) const;

		ISOReadAheadClass Runs[RUNS];
		std::size_t Order[RUNS];
		BoundType Declared[BOUNDS];
		std::size_t Written;
};


/*
 * Serves an image out of a URL.
 *
 * Reads arrive small and clustered -- a directory sector here, a mixfile header there --
 * and one request apiece would spend the whole run in round trips, so a read shorter than
 * a block is served from a block-sized window that is kept in a small least-recently-used
 * set. Whole blocks are asked for together, so an extent still costs one request rather
 * than one per window, and the two partial blocks at its ends come through the window that
 * covers them -- which is also what leaves every block either wholly fetched or not fetched
 * at all, and so fit to be stored.
 *
 * What has been fetched is kept in the browser's database, so a second run reads the same
 * blocks back instead of the network. The memory-resident set stays the small window set
 * above; the stored set is bounded separately and read a block at a time.
 *
 * A read that continues a run is answered differently. ISOReadRunsClass says which blocks
 * the run is about to want, and those are asked for without waiting: the request is left in
 * flight and the engine goes back to decoding, which is where the page hands the thread
 * back and the answer arrives. What lands before it is wanted costs the read nothing at all.
 * The asking happens before the read the window was opened by is paid for, so a run that is
 * being read faster than the network answers spends one round trip on two requests rather
 * than one apiece.
 *
 * How far in front of the reading that goes is not fixed. ISOLinkClass measures what a
 * request to this image costs out of the requests being made anyway, and the window, the
 * size of one request and the number outstanding all come from that: a disc on the same
 * machine is read very nearly as it was before any of this, and a disc on the far side of
 * the world is read a long way in front of the engine because that is the only thing that
 * covers the round trip.
 *
 * The file layer says when a run is a file, which is worth more than any amount of guessing:
 * an ISO9660 file is one run of consecutive sectors, so the reading is known to be forward
 * from the first byte and known to stop where the file does. It also says which files it
 * expects to want later. Those are fetched only while nothing is being read, and are given
 * up the instant a run wants the connection back.
 *
 * A server that ignores the range and answers with the entire image is rejected rather
 * than accommodated: every read would then cost the whole file.
 */
class ISOHttpSourceClass : public ISOBlockSourceClass
{
	public:
		ISOHttpSourceClass(void);
		virtual ~ISOHttpSourceClass(void) override;

		ISOHttpSourceClass(ISOHttpSourceClass const &) = delete;
		ISOHttpSourceClass & operator = (ISOHttpSourceClass const &) = delete;

		bool Open(char const * url);
		void Close(void);
		bool Is_Open(void) const {return(Length != 0);}

		/// <summary>The key the store holds this image's blocks and record under.</summary>
		std::string const & Store_Key(void) const {return(Slot);}

		/// <summary>The key that says whether stored blocks still belong to this image.</summary>
		std::string const & Store_Signature(void) const {return(Signature);}

		virtual bool Read_At(std::uint64_t offset, void * buffer, unsigned int length) override;
		virtual std::uint64_t Total_Size(void) override {return(Length);}
		virtual void Hint(ISOHintType kind, std::uint64_t offset, std::uint64_t length) override;

		enum {
			BLOCK_SIZE = ISO_BLOCK_SIZE,	// Bytes fetched for a read too short for its own request.
			BLOCK_CACHE = 32				// Windows kept, which bounds those at two megabytes.
		};

	private:

		struct BlockType {
			std::uint64_t Index;
			std::vector<unsigned char> Data;
		};

		// Whether the run has reached the point where the database may be waited on at all.
		enum StoreStateType {
			STORE_UNTRIED,
			STORE_READY,

			/*
			** Holding what it holds. The origin has refused a write, so nothing more is put
			** there, but what is already there is still read back: a store that has run out
			** of room is worth every block it managed to keep, and giving those up as well
			** would make a full store slower than an empty one.
			*/
			STORE_FULL,
			STORE_OFF
		};

		// Blocks staged before the batch is written. It bounds what an unwritten batch costs
		// in memory, and bounds what a run that stops loses to a batch it never wrote. Each
		// batch is one database transaction, and the guessing hands over far more than the
		// reading does, so it is sized for that rather than for the reading alone.
		enum {
			STORE_BATCH = 32
		};

		// How long a partly filled batch waits for the loading to resume before it is
		// written anyway, in milliseconds.
		static constexpr double STORE_IDLE = 250.0;

		/*
		** What one image may be told to fetch while nothing is being read of it. The queue is
		** the runs the engine said it would probably want -- every archive it registers, and
		** the handful of files it knows it is about to open -- and the budget is what those
		** are allowed to cost.
		**
		** Nothing here decides how much of a run is worth taking. The engine says that when
		** it names the run, because what an archive is for is the only thing that separates
		** one read across a session from one seeked into once; see PrefetchType. The budget
		** is the outer bound on being told wrong, and is the figure an image may keep, since
		** guessing at more than can be kept spends a connection on bytes the next run would
		** have to fetch again anyway.
		*/
		enum {
			SOON_QUEUE = 64
		};

		static constexpr std::uint64_t SOON_BUDGET = ISOBlockIndexClass::STORE_MAX;

		// How many runs of blocks the store does not hold one queued file may be cut into.
		// A run interrupted part way through leaves holes, and asking for the holes rather
		// than for the file around them is what keeps a second attempt from paying twice.
		enum {
			SOON_RUNS = 8
		};

		// And how many blocks of it are moved into the store per read. The copying is done
		// inside a read, so it is held to what a read can afford to carry; a whole disc
		// passing through this is what sets it rather than the handful a menu asks for.
		enum {
			SOON_KEEP = 16
		};

	/*
	** How much of what has been fetched may be bytes nobody read. Reading ahead is a
	** guess, and a guess is paid for on somebody's connection, so what the guessing has
	** cost is held to a share of what has been fetched: over it, the window closes to a
	** single request until the reading catches up. It is the one bound that does not depend
	** on the guessing being right, which is why it is the outer one -- the link decides how
	** far ahead is worth reaching and this decides how much of that is affordable. It is a
	** ceiling rather than a target, and is set above where the reading settles, because a
	** limit sitting on the ordinary operating point holds the window shut for the rest of
	** the run instead of only when the guessing has gone wrong.
	*/
	static constexpr double WASTE_SHARE = 0.10;
	static constexpr std::uint64_t WASTE_FLOOR = 1024ull * 1024ull;

		bool Transfer(std::uint64_t offset, void * buffer, unsigned int length);
		bool Fetch_Run(std::uint64_t offset, void * buffer, unsigned int length);
		BlockType const * Block(std::uint64_t index);

		void Look_Ahead(void);
		bool Ahead_Serve(std::uint64_t offset, void * buffer, unsigned int length);
		void Ahead_Drop(void);
		void Ahead_Drop(std::uint64_t first, std::uint64_t stop);
		void Soon(std::uint64_t offset, std::uint64_t length);
		void Soon_Keep(void);

		bool Store_Ready(void);
		bool Store_Serve(std::uint64_t offset, void * buffer, unsigned int length);
		void Store_Keep(std::uint64_t offset, void const * buffer, unsigned int length,
			ISOBlockIndexClass::AdmitType how);
		void Store_Write(void);
		void Store_Drop(std::vector<std::uint64_t> const & evicted);
		void Store_Discard(void);

		/// <summary>Writes any open image's batch that has been left sitting.</summary>
		/// <remarks>A disc the game has finished with is read no more, so its own reads can
		/// no longer be what flushes it. The disc still being read carries it instead.</remarks>
		static void Store_Settle(void);

		std::string Url;
		std::uint64_t Length;
		std::vector<BlockType> Cache;

		// Where this image's figures are counted, since every image is read separately and
		// a block number means nothing without the image it belongs to.
		std::size_t Meter;

		ISOReadRunsClass Ahead;
		ISOLinkClass Link;
		std::uint64_t Queued;

		std::string Signature;
		std::string Slot;
		std::string Removals;
		ISOBlockIndexClass Index;
		StoreStateType StoreState;
		unsigned int Staged;
		double StagedAt;

		// The blocks of the batch that has not been written yet. A write the origin refuses
		// leaves none of them stored, so they are the ones the index has to let go of; what
		// an earlier batch wrote is still there and is still served.
		std::vector<std::uint64_t> Staging;

};


/// <summary>Reports where the page or the host says the disc images are.</summary>
/// <param name="locations">Receives one URL or local path per image, in the order they are
/// to be searched, and nothing at all when none was named.</param>
/// <remarks>A page names them by setting Module.opentsImage before the module loads, as an
/// array of locations or as one string holding them separated by commas; under node the
/// OPENTS_IMAGE environment variable does the same. A string naming a single image is the
/// one-element case of that. A page that names none still gets the default location, since
/// a browser build has nowhere else to read from.</remarks>
void ISO_Image_Locations(std::vector<std::string> & locations);

/// <summary>Builds the block source that serves a location.</summary>
/// <param name="location">A URL, or a path on whatever filesystem the module has.</param>
/// <returns>An open source, or nothing when the location could not be read.</returns>
std::unique_ptr<ISOBlockSourceClass> ISO_Open_Location(char const * location);

#endif
