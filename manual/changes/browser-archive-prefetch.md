---
title: Fetch every disc archive but the films before the game asks for them
category: performance
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

The browser build now fetches whole archives in the background while the game is loading.
Every archive the engine registers is one it reads files out of all session long, so all of
them are fetched -- the game data, the expansion, the maps, the sidebar art, the music --
rather than only those under a size limit. The archives of video are the exception: a film
is streamed from its own entry and the rest of a disc's video may never be opened, so only
their directories are fetched. What separates the two is what the engine does with an
archive, not how large it is.

This is a deliberate trade of bandwidth for waiting. A first launch downloads considerably
more than the game strictly reads, and in return it stops far less often to wait for a
server. The fetching stays behind the reading throughout: it is abandoned the moment a read
or a playing movie wants the connection, only a few requests are ever outstanding, and
nothing already held is asked for again.

What arrives is written to the browser's store as it comes, so an interrupted first launch
keeps what it managed to get and a second launch reads it back instead of the network. How
much the store keeps now follows what the browser says the site is allowed to hold rather
than a fixed figure. A store that fills stops taking new blocks and goes on serving the ones
it has, and a site with little room to spare keeps what the game has read in preference to
what was fetched on speculation.
