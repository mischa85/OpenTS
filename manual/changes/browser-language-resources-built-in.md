---
title: Carry the language resources in the browser build instead of reading them off the disc
category: performance
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

The browser build no longer reads the disc's language library. Its strings and dialog
templates are now compiled from the project's own resource script and carried by the
program, which removes a read the browser made before the game could start and could
neither cache nor serve from its cache. The text is the same text the Windows build shows,
so a session started from a Tiberian Sun disc now reads the expansion's strings as well
rather than only those the disc's own library carried.
