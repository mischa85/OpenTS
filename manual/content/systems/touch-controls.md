---
title: Touch controls
summary: "Reads a finger as a mouse button in the browser build: one finger is the left button, holding it is the right button, and two fingers move the view."
category: interface-controls
keys: []
related:
  - type: system
    id: sidebar
  - type: using
    id: build-and-run
---

The browser build reads a touch screen as well as a mouse, and a device with both — an iPad
with a mouse attached — answers to either at any moment. Nothing here changes what a mouse
does. Which button does what, edge scrolling, and hovering are the same on every target.

## The gestures

Two rules decide every gesture. One finger is the left button; two fingers are the view.

| Gesture | What it is |
| --- | --- |
| Tap | A left click where the finger landed |
| Press and hold | A right click where the finger landed |
| Drag one finger | The left button held and carried |
| Drag two fingers | The map panned one for one under them |
| Tap with two fingers | A right click, kept as a second way to reach it |

A hold becomes the right click after roughly half a second, and it happens while the finger is
still down, so what it cancels goes away under the finger rather than after it is lifted. A
finger that travels more than about ten screen pixels before then is a drag instead, and can
no longer become either a tap or a hold.

Where the finger is decides nothing. The engine already knows what a button means over the
tactical view, the sidebar, the tab bar, a dialog and a menu, so a gesture is handed over as
the button it stands for and the engine answers as it would to a mouse. That is what makes
one finger dragged across the battlefield a selection band, across a dialog's slider a slider
drag, and across a cameo nothing at all.

## What a hold reaches

The right button is the game's cancel, and it cancels in one order wherever it is used: a
building waiting to be placed first, then repair mode, sell mode, power mode, superweapon
targeting and waypoint mode. With nothing left to cancel it deselects everything. Holding a
finger on the battlefield is therefore how a touch player backs out of whatever they are
doing, and how they deselect.

Holding a finger on a sidebar cameo is the same right click the desktop uses there: it puts
production on hold, and holding again abandons it and refunds the money.

## Nothing hovers

A mouse leaves a pointer wherever it stops, and a good deal of the interface reads that
resting position rather than a click: the tooltip that appears over terrain or a cameo, the
map scrolling when the pointer rests against an edge, and the placement cursor following the
pointer while a structure waits to be put down. A finger leaves nothing behind, so on touch
none of those fire, and the position of the last tap is never mistaken for somewhere the
player is pointing.

Placing a structure works differently for that reason. There is no pointer to carry the
outline about, so the first tap moves it to the tapped spot and the second tap puts the
building there. The player sees where it will land, and whether the game will allow it,
before it lands, and a hold cancels the placement outright.

## Movies and typing

A movie has no use for a button, so any touch during one stops the movie, exactly as the
escape key does. A movie the mission does not allow to be skipped still cannot be.

The hall of fame asks for a name at the end of a mission and ends only when Return is typed.
On a device whose only keyboard is drawn on its screen, the page asks for that keyboard while
the game is waiting for the name and puts it away afterwards. A phone or tablet will not raise
its keyboard except in answer to a gesture, so if it does not appear by itself, one tap
anywhere brings it up.

A text field in a dialog — the name in skirmish and multiplayer setup is the one most players
meet — asks for the same keyboard, for as long as the field holds the focus. Tapping the field
is what gives it the focus, and the keyboard goes away when the focus moves to another control
or the dialog closes. The same rule about a gesture applies: if the keyboard does not appear
with the tap that reached the field, the next one brings it up.

## Panning during a scripted sequence

Two fingers move the map directly rather than through the input the engine drops while it is
driving the camera, so the pan is suppressed for as long as the game has taken control of the
player — a scripted sequence in a mission, or an open in-game dialog. A mouse cannot scroll
then either.
