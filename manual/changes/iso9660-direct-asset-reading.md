---
title: Read game data straight from a disc image
category: feature
release: 0.2.0
targets: []
credit:
- Gunnar Beutner
---

A data search path entry may now name an ISO9660 disc image instead of a directory, so a
disc supplied as an image is read in place with no installation or extraction step. Each
image offers its installed data directory ahead of its root, and images are searched in the
order they were given, so listing the expansion disc first lets its newer archives win.
Names are matched without regard to case, whichever way the disc spells them.

A file read out of an image is read only: creating, deleting or writing one fails rather
than appearing to succeed. Directory search paths behave exactly as before.
