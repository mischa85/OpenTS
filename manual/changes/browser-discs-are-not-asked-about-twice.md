---
title: Open a disc a browser has already read without asking the server about it again
category: performance
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

The browser build used to open every disc image by asking the server what it was, and it did
so one disc at a time before it read a byte of the game. On a set of three discs that was
three round trips spent in front of everything else, on every launch, however much of the
discs the browser was already holding.

What that request answered -- how long the image is and what the server calls this version
of it -- is now kept by the browser, under the address the disc was named by. A launch that
names the same discs opens them out of what it kept and asks the server
nothing at all, so the first thing that reaches the network is the game's own first read. A
disc at an address the browser has not seen, or one named for the first time, is asked about
exactly as before. What an earlier run measured of the connection is kept with it, so the
first read of a launch is fetched at the size the link was last found to want rather than at
the smallest one.

The trade is that a disc image replaced at an address that did not change is no longer
noticed when the game starts. It is checked for once the game is up and running, where the
check costs the player nothing: a disc that turns out to have been replaced is reported, and
the next launch reads it afresh and discards what it was holding for the old one.
