FROM ubuntu:24.04 AS build

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build-package -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/spwkit \
        -DBUILD_SHARED_LIBS=OFF \
        -DSPWKIT_BUILD_TESTS=OFF \
        -DSPWKIT_BUILD_CPP_TESTS=OFF \
        -DSPWKIT_BUILD_EXAMPLES=OFF \
        -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
        -DSPWKIT_BUILD_SIMULATOR=OFF \
        -DSPWKIT_BUILD_UDP=ON \
        -DSPWKIT_BUILD_DEVICE=OFF \
        -DSPWKIT_BUILD_VSPWD=OFF \
        -DSPWKIT_BUILD_TOOLS=OFF \
        -DSPWKIT_ENABLE_HEAP=ON \
        -DSPWKIT_ENABLE_CPP=ON \
    && cmake --build build-package --parallel \
    && cmake --install build-package

RUN CC=gcc CXX=/bin/false cmake -S examples/distributed -B build-peer-c -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=/opt/spwkit \
        -DCMAKE_C_FLAGS="-Wall -Wextra -Werror" \
    && ! grep -q '^CMAKE_CXX_COMPILER:' build-peer-c/CMakeCache.txt \
    && cmake --build build-peer-c --parallel

RUN cmake -S examples/distributed_cpp -B build-peer-cpp -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=/opt/spwkit \
        -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" \
    && cmake --build build-peer-cpp --parallel

FROM ubuntu:24.04

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build-peer-c/spwkit_udp_peer /usr/local/bin/spwkit_udp_peer_c
COPY --from=build /src/build-peer-cpp/spwkit_udp_peer_cpp /usr/local/bin/spwkit_udp_peer_cpp
COPY tests/d2d/docker/peer-entrypoint.sh /usr/local/bin/spwkit-peer-entrypoint
RUN chmod +x /usr/local/bin/spwkit-peer-entrypoint

ENTRYPOINT ["/usr/local/bin/spwkit-peer-entrypoint"]
