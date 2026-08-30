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

An image is now identified by where the page was told to find it. Whether the parts already
kept may still be served is decided as it was before, by the length the server reports and
the version it names, so an image that has genuinely changed is still read afresh and two
images named separately are still kept apart.
