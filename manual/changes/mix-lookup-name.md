---
title: Look up a mix file entry without raising the caller's name
category: fix
release: 0.2.0
targets:
- type: format
  id: mix
  effect: changed
credit:
- mischa85
---

Looking up a file inside an archive no longer writes over the name it was given. The index is keyed on upper case names, and the lookup used to raise the caller's string in place before hashing it, which is undefined where the caller passes a literal and leaves a caller's buffer changed where it does not. Most callers do pass a literal. It raises a copy now, and the caller's string is left as it was.
