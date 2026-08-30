---
title: Measure score screen text from the character the font was given
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

The score screen font now measures and prints from the character it was handed rather than
from a buffer nothing had written. Its width and string routines asked the host to translate
a character into the code page the glyph shapes are ordered by, then read the destination
whether or not the translation had happened, so a host that could not perform it left the
font working from an indeterminate byte. That measurement is what the screen advances by
between glyphs and what it centres its labels and tallies on, so labels drifted apart and
figures landed on top of one another. The menu font's string drawing read the same unwritten
byte and is corrected with it. Text is unchanged where the translation takes place.
