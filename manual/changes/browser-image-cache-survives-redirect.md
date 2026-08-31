---
title: Keep a cached disc image when the server answers from another address
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

The browser build keeps some of a disc image it has read, so a later run starts without
fetching those parts again. What it had kept used to be filed under the address the answer
came from rather than the one the page named, and a large image is commonly served from a
pool that sends each request on to whichever machine is free. A run then looked for its
blocks under a name nothing had ever written to, found none, and read the image from the
beginning again.

An image is now identified by where the page was told to find it, together with how long the
image is. Those two decide whether the parts already kept may still be served, so an image
that has genuinely changed is still read afresh and two images named separately are still
kept apart.

What no longer takes part in that decision is the version name a server puts on its answers.
That name is the server's to choose and says nothing about the bytes: a mirror re-publishing
a disc, a second mirror of the same disc, or a content delivery network renewing its own
names each produce a new one for a file that has not changed since the game shipped. A
browser holding a large part of three discs used to throw all of it away and fetch it again
whenever that happened, which is the failure this guards against rather than a real one.
Because what identifies an image has changed, a browser holding disc data from an earlier
build discards it once and reads those parts again.
