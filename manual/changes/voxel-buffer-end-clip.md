---
title: Clip voxel drawing at the end of the voxel buffer
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

A voxel that lands on the last byte of the voxel drawing buffer no longer writes past the
end of it. Every drawer but the two unlit ones for artwork without normals covers a pair of
buffer bytes per voxel, and at that one position the second byte of the pair, and its depth
buffer counterpart, fell outside the buffer and corrupted whatever followed. The pair is now
clipped to the buffer, and a voxel anywhere else draws exactly what it drew before. The
object shadow drawer covered the same pair and is clipped with it.
