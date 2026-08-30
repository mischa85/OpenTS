---
title: Scale the browser build's disc read ahead to the connection it is reading over
category: performance
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

The browser build reads its discs over the network, and how far in front of the game it
fetches is now measured rather than fixed. A disc served from the same machine is read very
nearly as it was before; a disc served from a distant host is read much further ahead,
because a round trip there costs hundreds of times more and nothing else covers it. The
file layer also tells the reader where each file it opens begins and ends, so reading ahead
starts at the first byte of a file rather than after the reads it used to take to notice the
pattern, and stops at the end of that file rather than running into the next one. How much
of a disc may be fetched and never read is capped, so a guess that goes wrong costs a share
of the connection rather than as much as the link would carry.

Game data, saves and the bytes delivered are unchanged. What changes is how much of a disc
is fetched before it is asked for, which a slow connection now spends more of and a fast one
spends about what it did. A movie streamed from a distant server no longer stalls at the
point the old fixed window ran dry.
