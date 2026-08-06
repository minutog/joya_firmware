FROM ubuntu:24.04

ARG ZEPHYR_VERSION=v4.2.0
ARG ZEPHYR_SDK_VERSION=0.17.4
ARG WEST_VERSION=1.5.0

ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

ENV ZEPHYR_WORKSPACE=/opt/zephyrproject
ENV ZEPHYR_BASE=/opt/zephyrproject/zephyr
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-0.17.4
ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr

ENV PATH="/opt/venv/bin:${PATH}"

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y \
        --no-install-recommends \
        ca-certificates \
        ccache \
        cmake \
        curl \
        file \
        g++ \
        gcc \
        git \
        gperf \
        make \
        ninja-build \
        python3 \
        python3-dev \
        python3-pip \
        python3-venv \
        srecord \
        wget \
        xz-utils \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/venv \
    && pip install --no-cache-dir --upgrade pip setuptools wheel \
    && pip install --no-cache-dir "west==${WEST_VERSION}"

RUN west init \
        -m https://github.com/zephyrproject-rtos/zephyr \
        --mr "${ZEPHYR_VERSION}" \
        "${ZEPHYR_WORKSPACE}" \
    && cd "${ZEPHYR_WORKSPACE}" \
    && west update \
    && west zephyr-export \
    && west packages pip --install

RUN wget \
        "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64.tar.xz" \
        -O /tmp/zephyr-sdk.tar.xz \
    && tar -xf /tmp/zephyr-sdk.tar.xz -C /opt \
    && rm /tmp/zephyr-sdk.tar.xz


COPY docker/JLink_Linux_V966_x86_64.deb /tmp/jlink.deb

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y \
        --no-install-recommends \
        usbutils \
        libusb-1.0-0 \
        libusb-1.0-0-dev \
    && dpkg-deb -x /tmp/jlink.deb / \
    && ln -sf /opt/SEGGER/JLink/JLinkExe /usr/local/bin/JLinkExe \
    && ln -sf /opt/SEGGER/JLink/JLinkGDBServerExe /usr/local/bin/JLinkGDBServerExe \
    && rm -f /tmp/jlink.deb \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace/app

CMD ["/bin/bash"]