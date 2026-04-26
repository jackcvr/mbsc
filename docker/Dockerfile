FROM python:3-alpine

# common deps
RUN apk add --no-cache \
    build-base \
    gdb \
    wget \
    cmake \
    autoconf \
    automake \
    libtool \
    git \
    pkgconf \
    linux-headers \
    socat

RUN apk add --no-cache zig \
    && printf '#!/bin/sh\nexec zig cc "$@"\n' > /usr/local/bin/zig-cc \
    && printf '#!/bin/sh\nexec zig c++ "$@"\n' > /usr/local/bin/zig-c++ \
    && chmod +x /usr/local/bin/zig-*

# slave deps
RUN pip install pymodbus[serial]

# project deps
RUN apk add --no-cache \
    readline-dev \
    ncurses-static \
    readline-static \
    gtest-dev \
    clang-extra-tools

WORKDIR /app

ENV CC="zig-cc" \
    CXX="zig-c++" \
    CFLAGS="-Oz -flto -I/usr/include -DHAVE_GETRANDOM=1 -include sys/random.h" \
    CXXFLAGS="-Oz -flto -isystem /usr/include" \
    LDFLAGS="-s -L/usr/lib"
