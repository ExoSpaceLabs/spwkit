FROM ubuntu:24.04 AS build

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential cmake ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -S benchmarks/transport -B /build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build --parallel --target spwkit_udp_perf spwkit_raw_ethernet_perf

FROM ubuntu:24.04
COPY --from=build /build/spwkit_udp_perf /usr/local/bin/spwkit_udp_perf
COPY --from=build /build/spwkit_raw_ethernet_perf /usr/local/bin/spwkit_raw_ethernet_perf
COPY benchmarks/transport/docker/endpoint.sh /usr/local/bin/endpoint.sh
RUN chmod +x /usr/local/bin/endpoint.sh
ENTRYPOINT ["/usr/local/bin/endpoint.sh"]
