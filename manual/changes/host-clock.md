---
title: Read the coarse clock from the host instead of winmm
category: internal
release: 0.2.0
targets: []
credit:
- mischa85
---

The engine's millisecond clock now comes from the host's steady clock rather than winmm's `timeGetTime`. It still counts milliseconds and still wraps about every forty nine days, so code that compares two readings behaves as before.

The clock paces animation and frame timing and supplies the average processing time each player reports for frame rate negotiation. It does not advance the simulation, so saves and replays are unaffected, and a game without an explicit seed still takes its random seed from the clock as it did before. `timeBeginPeriod` and `timeEndPeriod` stay on Windows, where they now set only timer and sleep granularity.
