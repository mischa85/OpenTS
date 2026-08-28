---
title: Keep fogged object redraws inside safe bounds
category: fix
release: 0.2.0
credit: [Krisztiaan]
targets:
- type: system
  id: map-visibility
  effect: changed
---

Fogged structures, terrain, overlays and smudges are now clipped to the tactical view
when a cell at its edge asks to be redrawn. A cell's redraw area extends beyond its own
diamond, and one along the top, bottom or sides could therefore pass an out-of-frame
clipping window into the shape renderer. The renderer then addressed pixels outside the
frame and could stop the game while loading or playing a fog-of-war scenario.

Fogged buildings that use their owner's palette now also reuse that owner's color
converter, matching visible buildings. They previously treated the map cell as a color
converter, which could overwrite the cell with rendering state and stop the game during
a later terrain-overlay redraw.
