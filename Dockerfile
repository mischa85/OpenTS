# syntax=docker/dockerfile:1

# The browser target, built and then served as static files. The game data is not part of
# this image and never should be: the discs are mounted in at run time by whoever has them.

# The tag pins the Emscripten the tree is developed against. The build is the one
# docs/BUILDING.md describes for a page, so NODERAWFS is off.
#
# The engine is built twice, because how a wait hands the thread back is a link-time
# decision and no one module runs everywhere: the JSPI build is the one to run and the
# Asyncify build is what a browser without JSPI falls back to. Both land in one directory
# and the page loads whichever the browser can run.
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

RUN emcmake cmake -S . -B build-asyncify -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DOPENTS_WASM_NODERAWFS=OFF \
        -DOPENTS_WASM_SUSPEND=ASYNCIFY \
        -DCMAKE_CXX_FLAGS_RELEASE="-O1 -DNDEBUG" \
        -DCMAKE_EXE_LINKER_FLAGS="-sEXPORTED_RUNTIME_METHODS=FS,callMain" \
 && ninja -C build-asyncify OpenTS


# Nothing here needs to be a program: the page reads the discs with HTTP range requests, so
# a static server that answers them is the whole requirement.
FROM nginx:alpine AS serve

COPY --from=build /src/build/bin/index.html /src/build/bin/Game.js /src/build/bin/Game.wasm /usr/share/nginx/html/
COPY --from=build /src/build-asyncify/bin/Game-asyncify.js /src/build-asyncify/bin/Game-asyncify.wasm /usr/share/nginx/html/

# A redirect carries only the path, so the browser resolves it against the origin it
# actually used. nginx would otherwise build an absolute one out of what it sees itself,
# which behind a TLS-terminating proxy is plain HTTP and the container's own address; it
# does not consult X-Forwarded-Proto or X-Forwarded-Host to correct that.
RUN printf 'absolute_redirect off;\n' > /etc/nginx/conf.d/proxy.conf

EXPOSE 80
