---
title: Say why the browser build stopped when initialization fails
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

A browser session whose initialization fails now puts up the same message the Windows build
does and waits for it to be dismissed. The engine asks for that message through the form of
message box that takes a parameter block, and that form was not implemented on this target,
so the page simply went quiet and gave no reason. The text, the caption and the button are
the ones the Windows build shows. A session that starts is unaffected.
