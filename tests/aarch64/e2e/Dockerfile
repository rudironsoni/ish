# Docker image for aarch64 iSH E2E testing
# Provides cross-compilation and QEMU emulation environment

FROM debian:bookworm-slim

# Install dependencies
RUN apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu \
    gcc \
    make \
    meson \
    ninja-build \
    libsqlite3-dev \
    qemu-user-static \
    qemu-system-arm \
    libc6-dev-arm64-cross \
    wget \
    tar \
    xz-utils \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# Install musl cross compiler for static binaries
RUN mkdir -p /opt/cross && cd /opt/cross && \
    wget https://musl.cc/aarch64-linux-musl-cross.tgz && \
    tar xzf aarch64-linux-musl-cross.tgz && \
    rm aarch64-linux-musl-cross.tgz

ENV PATH="/opt/cross/aarch64-linux-musl-cross/bin:${PATH}"

# Create test directories
RUN mkdir -p /tests/binaries /tests/results /tests/fixtures

WORKDIR /tests
