---
title: Play a mission transmission to the end in the browser
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

The video a mission plays in the radar pane no longer freezes a second or so in and hands the
radar back with most of the film unseen. A movie the game steps alongside itself takes its
time from how far its sound track has played, and in the browser build nothing carried that
sound track forward between frames, so the clock stopped and the player ran out of picture
well before the end. A transmission now runs at its own frame rate from the first frame to
the last, and the radar returns when it is over.

Full screen movies were never affected, and no other target reads its movie clock any
differently.
