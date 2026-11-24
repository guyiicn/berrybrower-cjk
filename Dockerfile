FROM sw7ft/bb10-gcc9@sha256:1b58f0b066986eee6a60ff10454bf549de83b6f31e31916e237767704649c853 AS blackberry
RUN set -e; archives=/root/toolchain-gcc9-archive/toolchain_transfer; \
    tar -xzf "$archives/qnx800_toolchain_20251026.tar.gz" -C /root; \
    mkdir -p /root/bbndk/gcc9; \
    tar -xzf "$archives/gcc9_build_20251026.tar.gz" -C /root/bbndk/gcc9

FROM ubuntu:24.04
RUN dpkg --add-architecture i386 && apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    clang-17 lld-17 ninja-build build-essential ca-certificates file git nodejs \
    python3 python3-setuptools pkg-config patchelf libc6:i386 libstdc++6:i386 \
    openjdk-8-jre-headless && rm -rf /var/lib/apt/lists/*
COPY --from=blackberry /root/qnx800 /root/qnx800
COPY --from=blackberry /root/bbndk /root/bbndk
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    libnss3-dev libglib2.0-dev libdbus-1-dev libexpat1-dev libfontconfig1-dev \
    libx11-dev libxext-dev libxkbcommon-dev && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y --no-install-recommends gperf && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y --no-install-recommends python3-html5lib && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y --no-install-recommends llvm-17 && rm -rf /var/lib/apt/lists/*

# The V8 ARM simulator tools are built as 32-bit Linux executables.
RUN apt-get update && apt-get install -y --no-install-recommends g++-multilib && rm -rf /var/lib/apt/lists/*
