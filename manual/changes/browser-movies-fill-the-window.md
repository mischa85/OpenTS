---
title: Fill the browser window with a full screen movie
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

**Stretch movies to fit resolution** now starts out on in the browser build. The frame there
is the page's canvas rather than a resolution anyone picked, so a movie played at its own
size was a small picture in the middle of the window however large the window was — on a
tablet, about a fifth of the screen — and the display options that carry the switch are not
reachable in that build to turn it on. Every other target keeps the setting off unless
`sun.ini` or the display options say otherwise.

Menu backdrops and title screens are unaffected, as they always were: a shell screen places
its lettering and its choices against artwork of a fixed size, so that artwork stays the size
it is drawn at.
