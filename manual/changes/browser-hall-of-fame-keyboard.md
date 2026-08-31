---
title: The hall of fame raises a keyboard
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

A mission's score screen asks for a name for the hall of fame and moves on only when Return is
typed. In the browser build on a device with no keyboard but the one drawn on its screen there
was no way to type it and no way past the screen, so a campaign ended there for good.

The engine now says while it is waiting for that name, and the page asks for its own keyboard
for as long as it waits; a device that will not raise one except in answer to a gesture does so
on the next tap. What is typed reaches the game through the ordinary key queue, including the
characters a software keyboard reports without a key event of their own. Nothing changes on a
target with a real keyboard.
