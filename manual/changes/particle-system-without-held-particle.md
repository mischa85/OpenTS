---
title: Let a spark or railgun particle system hold no particle type
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

A spark or railgun particle system whose type names no particle to hold no longer reads
through the particle it failed to create. Both effects asked their system for a fresh
particle each time round the spawn loop and used the answer without looking at it, and a
system that holds nothing has none to give; the spark burst also read its velocities from a
particle type list entry that does not exist. Such a system now simply spawns nothing. A
system that names a particle to hold spawns exactly the shower it spawned before.
