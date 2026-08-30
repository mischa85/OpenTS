---
title: Stretch movies on every display, and fit them to it
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

Turning on **Stretch movies to fit resolution** now enlarges a full screen movie on every
display. Enlarging a picture during a blit was carried by a Windows drawing call, and where
that call was unavailable — the browser build has no GDI — the request fell through to a
copy that could not resize: the movie was laid into the corner of the display at its own
size, cut off partway down, with the rest of the screen left black. The surfaces resize the
picture themselves now, by nearest neighbor so that the artwork keeps its own pixels, and
the Windows drawing call is no longer used for it.

A stretched movie also fits the display in both directions instead of only matching its
width. A display wider than the movie's own shape now leaves a margin at the sides rather
than pushing the top and bottom of the picture off the screen. Every display where the
previous sizing was already correct — every 4:3 and 5:4 resolution — is unchanged.

Menu backdrops are unaffected, as they always were: a menu draws its lettering and artwork
against the backdrop at a fixed size, so the backdrop plays at its own size whether the
option is on or off.
