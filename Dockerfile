# syntax=docker/dockerfile:1
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG TARGETARCH
ARG ZEPHYR_REF=main
ARG USERNAME=dev
ARG UID=1000
ARG GID=1000

RUN apt-get update && apt-get install -y --no-install-recommends \
        git cmake ninja-build gperf ccache dfu-util device-tree-compiler \
        wget curl ca-certificates xz-utils file make gcc g++ \
        python3 python3-dev python3-pip python3-venv python3-setuptools python3-wheel \
        libmagic1 libsdl2-dev \
    && if [ "$TARGETARCH" = "amd64" ]; then \
         apt-get install -y --no-install-recommends gcc-multilib g++-multilib; \
       fi \
    && rm -rf /var/lib/apt/lists/*

# Non-root user matching the host UID/GID (macOS gid 20 collides with
# Ubuntu's "dialout", so reuse an existing group instead of failing)
RUN (userdel -r ubuntu 2>/dev/null || true) \
    && (getent group ${GID} >/dev/null || groupadd --gid ${GID} ${USERNAME}) \
    && useradd --uid ${UID} --gid ${GID} -m -s /bin/bash ${USERNAME}

USER ${USERNAME}
RUN mkdir -p /home/${USERNAME}/.ccache /home/${USERNAME}/.cache

WORKDIR /home/${USERNAME}

ENV VIRTUAL_ENV=/home/${USERNAME}/venv
RUN python3 -m venv ${VIRTUAL_ENV}
ENV PATH="${VIRTUAL_ENV}/bin:${PATH}"

RUN pip install --no-cache-dir -U pip west

# Fetch the Zephyr Python requirements for the chosen revision.
# base is mandatory (fails the build if missing); run-test is best-effort.
RUN curl -fsSL -o /tmp/requirements-base.txt \
    "https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/${ZEPHYR_REF}/scripts/requirements-base.txt"
RUN pip install --no-cache-dir -r /tmp/requirements-base.txt

RUN curl -fsSL -o /tmp/requirements-run-test.txt \
    "https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/${ZEPHYR_REF}/scripts/requirements-run-test.txt" \
    && pip install --no-cache-dir -r /tmp/requirements-run-test.txt \
    || echo "no run-test requirements for ${ZEPHYR_REF}, skipping"

# Fetch all Zephyr requirements files for this revision (they reference each other)
RUN mkdir -p /tmp/zreq && cd /tmp/zreq \
    && for f in requirements.txt requirements-base.txt requirements-build-test.txt \
                requirements-run-test.txt requirements-extras.txt requirements-compliance.txt; do \
         curl -fsSL -o "$f" \
           "https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/${ZEPHYR_REF}/scripts/$f" \
           || echo "skipped $f (not present in ${ZEPHYR_REF})"; \
       done \
    && pip install --no-cache-dir -r requirements-base.txt \
    && pip install --no-cache-dir -r requirements-build-test.txt \
    && pip install --no-cache-dir -r requirements-run-test.txt \
    && pip install --no-cache-dir junitparser

# native_sim is built with the host gcc, no Zephyr SDK needed
ENV ZEPHYR_TOOLCHAIN_VARIANT=host
ENV ZEPHYR_BASE=/workspace/zephyr
ENV CCACHE_DIR=/home/${USERNAME}/.ccache

WORKDIR /workspace
CMD ["/bin/bash"]