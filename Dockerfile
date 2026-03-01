FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    librtlsdr-dev \
    libpaho-mqtt-dev \
    libpaho-mqttpp-dev \
    libyaml-cpp-dev \
    nlohmann-json3-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt .
COPY src/ src/
COPY include/ include/
COPY libs/ libs/

RUN mkdir build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release .. \
    && make -j"$(nproc)"

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    librtlsdr0 \
    libpaho-mqtt1.3 \
    libpaho-mqttpp3-1 \
    libyaml-cpp0.7 \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/adsb-module /usr/local/bin/adsb-module

ENTRYPOINT ["adsb-module"]
CMD ["/config/adsb-module.yaml"]
