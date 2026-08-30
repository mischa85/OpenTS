# syntax=docker/dockerfile:1

# The browser target, built and then served as static files. The game data is not part of
# this image and never should be: the discs are mounted in at run time by whoever has them.

# The tag pins the Emscripten the tree is developed against. The build is the one
# docs/BUILDING.md describes for a page, so NODERAWFS is off.
FROM emscripten/emsdk:6.0.8 AS build

RUN apt-get update \
 && apt-get install --no-install-recommends --yes ninja-build \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN emcmake cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DOPENTS_WASM_NODERAWFS=OFF \
        -DCMAKE_CXX_FLAGS_RELEASE="-O1 -DNDEBUG" \
        -DCMAKE_EXE_LINKER_FLAGS="-sEXPORTED_RUNTIME_METHODS=FS,callMain" \
 && ninja -C build OpenTS


# Nothing here needs to be a program: the page reads the discs with HTTP range requests, so
# a static server that answers them is the whole requirement.
FROM nginx:alpine AS serve

COPY --from=build /src/build/bin/index.html /src/build/bin/Game.js /src/build/bin/Game.wasm /usr/share/nginx/html/

EXPOSE 80
