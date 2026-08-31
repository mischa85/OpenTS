---
title: The wheel scrolls the sidebar in a browser
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

Turning the mouse wheel now scrolls the sidebar's build lists in the browser build, as it does
on the desktop. The page's wheel events were never picked up, so nothing reached the engine
and the lists could only be scrolled with their own arrows.

A wheel and a trackpad describe the same turn differently — in pixels, in lines or in pages,
and in one large report or a stream of small ones — so what arrives is measured and the list
moves one step per notch's worth of it, whichever the device sends.
