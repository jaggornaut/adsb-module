FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    pkg-config \
    libusb-1.0-0-dev \
    libpaho-mqtt-dev \
    libpaho-mqttpp-dev \
    libyaml-cpp-dev \
    nlohmann-json3-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/rtlsdrblog/rtl-sdr-blog /tmp/rtl-sdr-blog \
    && cmake -S /tmp/rtl-sdr-blog -B /tmp/rtl-sdr-blog/build \
        -DCMAKE_BUILD_TYPE=Release -DDETACH_KERNEL_DRIVER=ON \
    && make -C /tmp/rtl-sdr-blog/build -j"$(nproc)" install

WORKDIR /src
COPY CMakeLists.txt .
COPY src/ src/
COPY include/ include/
COPY libs/ libs/

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && make -C build -j"$(nproc)"

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libusb-1.0-0 \
    libpaho-mqtt1.3 \
    libpaho-mqttpp3-1 \
    libyaml-cpp0.7 \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local/lib/librtlsdr.so* /usr/local/lib/
COPY --from=builder /src/build/adsb-module /usr/local/bin/adsb-module
RUN ldconfig

ENTRYPOINT ["adsb-module"]
CMD ["/config/adsb-module.yaml"]
