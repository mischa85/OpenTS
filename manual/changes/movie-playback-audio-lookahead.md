---
title: Play movies at their own speed in the browser build
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

Movies in the browser build now run at the speed they were recorded at instead of dragging
along at roughly two thirds of it with a break in the sound every fiftieth of a second. A
movie paces itself against how much of its own soundtrack has been heard, so a soundtrack
that keeps stopping is a movie that keeps stopping with it. The page's audio output is
handed the sound a fixed distance in advance, and the distance the engine was using — a
fraction of a buffer sized for hardware that reads its own audio — worked out shorter than
the gap between two passes of the browser's own audio scheduler, so the output ran dry over
and over. The engine now measures that distance in milliseconds rather than in fractions of
a buffer, and asks for a movie sound buffer long enough to hold it. A fifty-three second
movie took seventy-nine seconds to play and now takes fifty-three. Sound effects and music
are unaffected, and nothing changes outside the browser build.
