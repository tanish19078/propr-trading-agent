# Reproducible build environment for propr-agent.
# Ubuntu 24.04 + GCC 13 + CMake + vcpkg manifest mode.
# Build:  docker build -t propr-agent .
# Iterate: docker run --rm -it -v "$(pwd):/work" -v propr-vcpkg:/opt/vcpkg/installed \
#           --env-file .env propr-agent make test
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH=$VCPKG_ROOT:$PATH

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      g++-13 \
      gcc-13 \
      cmake \
      git \
      curl \
      zip \
      unzip \
      tar \
      pkg-config \
      ninja-build \
      ca-certificates \
      python3 \
      autoconf \
      automake \
      libtool \
      libssl-dev \
      libsqlite3-dev \
      jq \
      vim-tiny \
 && rm -rf /var/lib/apt/lists/*

RUN update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100 && \
    update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100

# Clone vcpkg at a known baseline. Pin to a recent release for reproducibility.
ARG VCPKG_REF=2025.04.09
RUN git clone --depth 1 --branch ${VCPKG_REF} https://github.com/microsoft/vcpkg.git $VCPKG_ROOT \
 && $VCPKG_ROOT/bootstrap-vcpkg.sh -disableMetrics

# Pre-warm vcpkg cache with the dependency manifest BEFORE bind-mounting source so
# repeated `docker run` rebuilds skip dep compilation.
WORKDIR /opt/prewarm
COPY vcpkg.json .
RUN $VCPKG_ROOT/vcpkg install --triplet x64-linux

WORKDIR /work
ENV CMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake

# Default to a help message so accidental `docker run` doesn't do anything destructive.
CMD ["bash", "-c", "echo 'Usage: docker run --rm -v $(pwd):/work propr-agent make {build,test,smoke-net}'"]
