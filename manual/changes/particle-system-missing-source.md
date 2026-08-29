---
title: Let a smoke particle system run without a source object
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

A smoke particle system with no source object no longer reads through the missing pointer.
A system rising from a building is created without a source, and a system whose source is
destroyed goes on rising until its last particle dies, so the per-frame smoke logic has
always had to cope with an absent source. It asked the absent source what kind of object it
was and leaned on the cast to answer nothing, which holds only as long as the compiler
leaves the check in place. The source is now tested before the question is asked. Where a
system sits, and the smoke it gives off, are unchanged.
