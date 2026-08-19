# syntax=docker/dockerfile:1.7

ARG UBUNTU_VERSION=22.04

FROM ubuntu:${UBUNTU_VERSION} AS build

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        dpkg-dev \
        ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build-package -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_SHARED_LIBS=ON \
        -DSPWKIT_BUILD_TESTS=OFF \
        -DSPWKIT_BUILD_CPP_TESTS=OFF \
        -DSPWKIT_BUILD_EXAMPLES=OFF \
        -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
        -DSPWKIT_BUILD_SIMULATOR=ON \
        -DSPWKIT_BUILD_UDP=ON \
        -DSPWKIT_BUILD_DEVICE=ON \
        -DSPWKIT_BUILD_VSPWD=ON \
        -DSPWKIT_BUILD_TOOLS=ON \
        -DSPWKIT_ENABLE_HEAP=ON \
        -DSPWKIT_ENABLE_CPP=ON \
        -DCMAKE_C_FLAGS="-Wall -Wextra -Werror" \
    && cmake --build build-package --parallel \
    && cpack --config build-package/CPackConfig.cmake -G DEB -B /out \
    && test "$(find /out -maxdepth 1 -name 'spwkit_*.deb' | wc -l)" -eq 1

FROM scratch AS package-export
COPY --from=build /out/ /

FROM ubuntu:${UBUNTU_VERSION} AS validate

ARG DEBIAN_FRONTEND=noninteractive
COPY --from=build /out/ /tmp/spwkit-package/
RUN apt-get update \
    && apt-get install -y --no-install-recommends /tmp/spwkit-package/spwkit_*.deb \
    && rm -rf /var/lib/apt/lists/* /tmp/spwkit-package \
    && test -f /usr/include/spwkit/spwkit.h \
    && test -f /usr/include/spwkit/spwkit.hpp \
    && find /usr/lib -path '*/cmake/SpWKit/SpWKitConfig.cmake' -print -quit | grep -q . \
    && find /usr/lib -name 'libspwkit.so.0' -print -quit | grep -q . \
    && vspwd --help >/dev/null 2>&1 \
    && spwctl --help >/dev/null 2>&1 \
    && spwmon --help >/dev/null 2>&1

FROM validate AS runtime

ARG VERSION=unreleased
LABEL org.opencontainers.image.title="SpWKit" \
      org.opencontainers.image.description="SpaceWire Development & Integration Toolkit Linux runtime and tools" \
      org.opencontainers.image.version="${VERSION}" \
      org.opencontainers.image.source="https://github.com/ExoSpaceLabs/spwkit" \
      org.opencontainers.image.licenses="Apache-2.0"

RUN mkdir -p /run/spwkit
VOLUME ["/run/spwkit"]
ENTRYPOINT ["/usr/bin/vspwd"]
CMD ["--socket", "/run/spwkit/vspwd.sock"]