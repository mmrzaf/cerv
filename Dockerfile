ARG BASE_IMAGE=debian:trixie
FROM ${BASE_IMAGE} AS cerv-base

FROM cerv-base AS build
ARG DEBIAN_MIRROR=http://linux-mirror.liara.ir/repository/debian
ARG DEBIAN_SECURITY_MIRROR=http://linux-mirror.liara.ir/repository/debian-security

RUN set -eu; \
    printf '%s\n' \
      'Types: deb' \
      "URIs: ${DEBIAN_MIRROR}" \
      'Suites: trixie trixie-updates' \
      'Components: main' \
      'Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg' \
      '' \
      'Types: deb' \
      "URIs: ${DEBIAN_SECURITY_MIRROR}" \
      'Suites: trixie-security' \
      'Components: main' \
      'Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg' \
      > /etc/apt/sources.list.d/debian.sources; \
    apt-get update; \
    apt-get install -y --no-install-recommends build-essential; \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY Makefile VERSION RELEASE_EPOCH ./
COPY src ./src
COPY test/unit/test_main.c ./test/unit/test_main.c
COPY tools/check_elf.sh ./tools/check_elf.sh
RUN make -j"$(nproc)" release hardening-check \
    && build/release/unit \
    && install -D -m 0755 build/release/cerv /out/cerv \
    && strip --strip-unneeded /out/cerv

FROM cerv-base AS runtime
COPY --from=build /out/cerv /usr/local/bin/cerv
RUN mkdir -p /srv/cerv \
    && chmod 0555 /srv/cerv

USER 65532:65532
WORKDIR /srv/cerv

ENV CERV_LISTEN=0.0.0.0:8080 \
    CERV_ROOT=/srv/cerv \
    CERV_WORKERS=auto \
    CERV_MAX_CONNECTIONS=auto

EXPOSE 8080
STOPSIGNAL SIGTERM
ENTRYPOINT ["/usr/local/bin/cerv"]
