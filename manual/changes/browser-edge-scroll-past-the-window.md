---
title: Keep scrolling when the pointer leaves the browser window
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

Holding the pointer against an edge of the browser window scrolls the map for as long as it
is held there, as it does in a full screen game. A pointer that carried on out of the window
used to end the scroll part way through a pan, because the page stopped reporting a position
and the engine no longer saw the pointer at an edge. The position is now held against the
edge it left through until the pointer comes back or the window loses the keyboard.
