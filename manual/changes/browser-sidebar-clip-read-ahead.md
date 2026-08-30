---
title: Read ahead of a clip playing in the sidebar
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

The short movies a mission plays in the sidebar radar used to stop the browser build for as
long as several server round trips took. The whole of such a clip is read in one burst as it
opens, which is a shorter run than the reading ahead waited for, so every part of it was
asked for only once the playback had reached it and paid for at full price. Only one run was
followed per disc image as well, so the mission reading its map, its artwork or its music
between the clip's own reads ended the run and abandoned what had been asked for in front of
it.

A run is now believed after two reads that move forward, several runs are followed at once so
that streams reading the same image no longer end each other, and the blocks in front of a run
are asked for before the read that revealed the run is paid for, so one round trip carries
both. The requests also keep out of the browser's own document cache, which serves one request
per address at a time and so had been making a request left in flight delay the very read it
was meant to get in front of.
