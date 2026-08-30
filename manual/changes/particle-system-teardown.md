---
title: Destroy every particle a particle system is holding
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

A particle system being destroyed now takes every particle it holds with it. Deleting a
particle detaches it from the system that holds it, and the teardown loop read the first
entry back after the delete had already removed it, so it dropped the particle that had
moved up into that place instead of destroying it. Half the particles of a dying system
therefore leaked, and a system holding a single particle read an entry that was no longer
there. Particles are now taken hold of before they are deleted.
