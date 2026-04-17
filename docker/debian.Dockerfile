FROM python:3-slim

ARG ZIG_VERSION=0.13.0

# common deps
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gdb \
    wget \
    cmake \
    autoconf \
    automake \
    libtool \
    git \
    pkg-config \
    linux-libc-dev \
    socat \
    xz-utils \
    ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# install zig
RUN dpkgArch="$(dpkg --print-architecture)" \
 && case "${dpkgArch##*-}" in \
      amd64) ZIG_ARCH="x86_64" ;; \
      arm64) ZIG_ARCH="aarch64" ;; \
      *) echo "Unsupported architecture"; exit 1 ;; \
    esac \
 && wget -O zig.tar.xz "https://ziglang.org/download/${ZIG_VERSION}/zig-linux-${ZIG_ARCH}-${ZIG_VERSION}.tar.xz" \
 && tar -xf zig.tar.xz -C /usr/local \
 && mv /usr/local/zig-linux-${ZIG_ARCH}-${ZIG_VERSION} /usr/local/zig \
 && rm zig.tar.xz \
 && ln -s /usr/local/zig/zig /usr/local/bin/zig \
 && printf '#!/bin/sh\nexec zig cc "$@"\n' > /usr/local/bin/zig-cc \
 && printf '#!/bin/sh\nexec zig c++ "$@"\n' > /usr/local/bin/zig-c++ \
 && chmod +x /usr/local/bin/zig-*

# slave deps
RUN pip install --no-cache-dir pymodbus[serial] --break-system-packages

# project deps + google tcmalloc (static archives included in -dev packages)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libreadline-dev \
    libncurses-dev \
    libgtest-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

ENV CC="zig-cc" \
    CXX="zig-c++" \
    CFLAGS="-Oz -flto -I/usr/include -DHAVE_GETRANDOM=1 -include sys/random.h" \
    CXXFLAGS="-Oz -flto -isystem /usr/include" \
    LDFLAGS="-s -L/usr/lib"
