---
title: Size the LCW compression buffers from the encoder's real worst case
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

The buffers the LCW block compressor writes into are now sized from what the encoder can
actually produce. They followed a documented promise that a block would grow by at most one
byte per 128, while the encoder can spend one byte per four; an ordinary incompressible
block of the default 8K size already ran past the end of both the pipe and the straw buffer.
Writing packed map data, which is where the pipe is reached, can no longer overrun them.
Compressed output itself is unchanged and data written earlier still reads back identically.
