---
title: Let music go quiet rather than stop the game waiting for a disc
category: performance
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

A score in the browser build no longer holds the game up while its next few seconds are
fetched. Music is streamed off the disc a block at a time, and where the disc is a web
server every block the read-ahead has not already covered used to stop the engine dead for a
round trip. Those reads now say the bytes are not here yet and ask for them without waiting,
so the track starts a moment late or, if the wait runs on past the several seconds already
buffered, ends early and the next one begins. Play carries on throughout either way.

Nothing else reads this way. Sound effects and speech are unaffected, an ordinary file read
still waits for what it asked for, and a disc whose bytes are at hand -- every target but a
browser reading its discs over a network -- withholds nothing and behaves exactly as before.
A score that has been waiting a long time falls back to waiting for the disc once, so a file
that genuinely cannot be read still reports itself rather than leaving the music silent for
the rest of the session.
