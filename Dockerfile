# syntax=docker/dockerfile:1.7

# Debian 13.6 slim, pinned to the official multi-architecture image index used
# for this release. Dependabot owns controlled base-image refreshes.
FROM debian:trixie-20260713-slim@sha256:020c0d20b9880058cbe785a9db107156c3c75c2ac944a6aa7ab59f2add76a7bd AS cerv-base

FROM cerv-base AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential \
    && rm -rf /var/lib/apt/lists/*
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

# Numeric non-root identity keeps the runtime independent of passwd/group files.
USER 65532:65532
WORKDIR /srv/cerv

# These are image defaults, not wrapper-script arguments. Cerv reads them natively.
ENV CERV_LISTEN=0.0.0.0:8080 \
    CERV_ROOT=/srv/cerv \
    CERV_WORKERS=auto \
    CERV_MAX_CONNECTIONS=auto

EXPOSE 8080
STOPSIGNAL SIGTERM
ENTRYPOINT ["/usr/local/bin/cerv"]
