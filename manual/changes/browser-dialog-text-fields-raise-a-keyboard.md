---
title: A dialog's text field raises a keyboard
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

The name in skirmish and multiplayer setup sits in a text field, and in the browser build on a
device with no keyboard but the one drawn on its screen nothing raised one for it, so the name
could not be changed.

A text field that takes the focus now asks the page for a keyboard, and gives it back when the
focus moves on or the dialog closes. The request follows the focus rather than being made by
any one screen, so every text field in the front end is covered by it. Nothing changes on a
target with a real keyboard.
