---
title: Read ahead of a movie playing off a disc image
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

A movie streamed off a disc image the browser had not already cached used to stutter. Each
part of the file was asked for only once the player had reached it, and the request stopped
the page for as long as the server took to answer, so a frame was late every time the
reading crossed into a part of the image that had not been fetched. The reading is now
followed, and when it settles into a run the image is asked for ahead of the player while
the frames already delivered are still being decoded, so the answer is usually there before
it is wanted.

The reading ahead follows a run only after it has proved itself and never reaches more than
a megabyte in front of it, so a file read out of order costs a window rather than a
download, and a part of the image the browser is already holding is not fetched again.
