---
title: Fetch a playing movie far ahead of it and read the answer as it arrives
category: performance
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

The browser build reads its discs over the network, and a file it has been told the bounds of
is now fetched a long way in front of the reading from that file's very first read. It used
to earn that distance a block at a time, which left the window narrower than a single one of
a movie player's own buffer top-ups, so nothing was ever in front of the reading and every
top-up cost a round trip. A file whose bounds are known is not a guess, so it no longer waits
on the estimate that produced that window; a run nobody declared still reaches no further
than it has already read.

Disc requests are now read as they arrive rather than when they finish. A read is answered
the moment the part of a request it is asking for has landed, so asking for a megabyte at a
time no longer postpones the frame that needed the first block of it, and a request that is
abandoned keeps every whole block that already crossed the wire.

Skipping a cutscene now stops the fetching for it at the keypress rather than when the movie
is torn down, so the rest of a film nobody is watching is not fetched. The archives the game
registers are also named to the disc reader as it registers them and fetched alongside the
loading, never waited on and never more than three requests at a time per disc; what arrives
is written to the browser's disc cache whether or not anything reads it. An archive too large
to be worth guessing at has only its head fetched, which is where it keeps the directory a
registration reads. Menu backdrop animations are named as the menu system starts rather than
as each page is built.

Game data, saves and the bytes delivered are unchanged. What changes is how much of a disc is
fetched before it is asked for. Reading the discs over a connection with a two second round
trip, reaching the main menu fell from 163 to 57 seconds and starting a campaign mission from
an empty cache fell from 518 to 317 seconds; the time the game spent waiting on the discs
fell from 150 to 44 seconds and from 323 to 120 seconds. Reaching the menu fetched 60 MiB
against 22 MiB before, of which 4 MiB was never read.

The status line's disc figures now include what the reading cost in time the game spent
waiting, and a page can read a run's stalls one line each for diagnosis.
