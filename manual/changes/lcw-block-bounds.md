---
title: Reject LCW blocks that do not fit the buffers they decompress into
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

Compressed blocks in map and saved game data are now checked before they are decompressed.
The sizes in a block header used to be believed outright, and every command inside the block
was executed without regard for where the destination buffer ended, so a truncated or
tampered file could drive the decompressor far past the buffers on either side. A block that
declares more than the reader can hold, or that does not decompress to the size it claims,
is now treated as the end of the data. Well formed data is unaffected, and the bytes read
back from it are unchanged.
