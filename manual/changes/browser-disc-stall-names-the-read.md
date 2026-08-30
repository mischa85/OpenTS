---
title: Record a disc stall against the read that waited, not the block it was served from
category: internal
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

The browser build's stall record now reports the bytes the engine asked for rather than the
span fetched to deliver them. A read shorter than a block is served out of the whole block
that holds it, and that block can begin as much as sixty-four kilobytes earlier, so the
record used to name the block instead of the read. Where a file begins part way into a
block, the block starts inside whatever the disc carries in front of that file, and the first
read of an archive was recorded against the installer or document sitting ahead of it.

Nothing about what is fetched, cached or stored changes; only the line the record writes.
