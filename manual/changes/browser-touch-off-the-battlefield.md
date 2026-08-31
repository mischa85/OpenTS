---
title: Keep touch to what the battlefield allows
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

Tapping a choice on a menu, briefing or score screen in the browser build now picks it. The
touch layer ended every gesture by putting the position back in the middle of the tactical
view, so that a finger left resting against an edge did not scroll the map for ever. It did
that on the shell screens too, where there is no tactical view and the position it used was
the one the game starts up with, and the tap it had just placed was thrown at a corner of the
window before the screen could read it.

A finger can also no longer pan the map or open a selection band while the game has taken
control of the player — a scripted sequence in a mission, or an open in game dialog. Those
gestures reach the map directly rather than through the input the engine drops while it is
driving, so they were the one way to move the camera during a cutscene. A mouse never could.
