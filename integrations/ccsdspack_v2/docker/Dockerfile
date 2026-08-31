# syntax=docker/dockerfile:1

FROM ubuntu:24.04 AS build

ARG DEBIAN_FRONTEND=noninteractive
ARG CCSDSPACK_TAG=v2.0.0
ARG CCSDSPACK_SHA=c2f318c330c564429bcc565a8acbff22728b2851

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        cmake \
        g++ \
        git \
        ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Keep the packet dependency independent from the SpWKit source tree and pin
# both its public release name and immutable commit.
RUN git clone --depth 1 --branch "${CCSDSPACK_TAG}" \
        https://github.com/ExoSpaceLabs/CCSDSPack.git ccsdspack \
    && test "$(git -C ccsdspack rev-parse HEAD)" = "${CCSDSPACK_SHA}"

RUN cmake -S ccsdspack -B ccsdspack-build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCCSDSPACK_BUILD_MCU=OFF \
        -DCMAKE_INSTALL_PREFIX=/opt/ccsdspack \
    && cmake --build ccsdspack-build --parallel \
    && cmake --install ccsdspack-build

COPY . /src/spwkit

# Build/install SpWKit independently. The integration below is deliberately
# configured only after installation so source-private targets cannot satisfy
# either dependency.
RUN cmake -S spwkit -B spwkit-build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DSPWKIT_BUILD_TESTS=OFF \
        -DSPWKIT_BUILD_CPP_TESTS=OFF \
        -DSPWKIT_BUILD_EXAMPLES=OFF \
        -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
        -DSPWKIT_BUILD_SIMULATOR=OFF \
        -DSPWKIT_BUILD_UDP=ON \
        -DSPWKIT_BUILD_DEVICE=ON \
        -DSPWKIT_BUILD_VSPWD=OFF \
        -DSPWKIT_BUILD_TOOLS=OFF \
        -DSPWKIT_BUILD_CUSE=OFF \
        -DSPWKIT_ENABLE_HEAP=ON \
        -DSPWKIT_ENABLE_CPP=ON \
        -DCMAKE_INSTALL_PREFIX=/opt/spwkit \
    && cmake --build spwkit-build --parallel \
    && cmake --install spwkit-build

RUN cmake -S spwkit/integrations/ccsdspack_v2 -B integration-build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="/opt/spwkit;/opt/ccsdspack" \
    && cmake --build integration-build --parallel

FROM ubuntu:24.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /opt/spwkit /opt/spwkit
COPY --from=build /opt/ccsdspack /opt/ccsdspack
COPY --from=build /src/integration-build/spwkit_ccsdspack_v2_peer \
    /usr/local/bin/spwkit_ccsdspack_v2_peer

ENV LD_LIBRARY_PATH=/opt/spwkit/lib:/opt/ccsdspack/lib
ENTRYPOINT ["/usr/local/bin/spwkit_ccsdspack_v2_peer"]
