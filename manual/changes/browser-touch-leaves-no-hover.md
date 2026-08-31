---
title: A finger no longer leaves a pointer behind
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

Structures can be placed where they were tapped, the in-game menu opens when its tab is
tapped, and no terrain tooltip appears over the middle of the screen. The touch layer used to
leave the reported position in the middle of the tactical view once a gesture was over, and
everything that reads a resting pointer answered to it: the map's own legality check ran at
the middle of the screen while the click placed the structure where the finger was, so a spot
was buildable only if the middle of the screen was too.

The engine is now told that nothing is resting anywhere while touch is what is driving it, so
a tooltip, an edge scroll and a placement cursor that follows the pointer all wait for a mouse.
Placing a structure by finger takes two taps, the first to position it and the second to
commit, which is what puts the outline and its verdict in front of the player before the
building lands. See [Touch controls](/systems/touch-controls/). Mouse play is unchanged.
