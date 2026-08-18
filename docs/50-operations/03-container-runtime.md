# Container Runtime and SPA Deployment

> **Status:** Normative deployment profile  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Official container image behavior and use as a static-application runtime base.

## Image contract

The repository-root `Dockerfile` is the canonical Cerv runtime image definition. It SHALL:

- use a multi-stage build so compiler/tooling is absent from the runtime stage;
- run the hardened release executable directly as PID 1 with exec-form `ENTRYPOINT`;
- run as a numeric non-root identity;
- expose port 8080 and default `CERV_LISTEN=0.0.0.0:8080`;
- default `CERV_ROOT=/srv/cerv`, `CERV_WORKERS=auto`, and `CERV_MAX_CONNECTIONS=auto`;
- declare `SIGTERM` as the container stop signal;
- contain no shell entrypoint that rewrites environment variables into CLI arguments.

The Dockerfile pins its Debian 13.6 slim base to an immutable multi-architecture image index. Base-image refreshes are explicit dependency updates so a release review can see exactly when the runtime foundation changes.

## Runtime isolation

Recommended production runtime controls remain external defense in depth:

- read-only container root filesystem;
- no added Linux capabilities / drop all where practical;
- runtime `no-new-privileges`;
- document-root bind mount read-only when content is mounted rather than baked into the image;
- CPU/memory/PID limits appropriate to the service;
- explicit `nofile` limit when a numeric high connection cap is requested.

Cerv's own `openat2()` confinement, seccomp profiles, `no_new_privs`, and optional Landlock remain active inside the container. If an outer container seccomp policy rejects the Landlock ABI probe with `EPERM`, Cerv records Landlock as unavailable and continues with its mandatory confinement/seccomp layers; the optional defense-in-depth layer does not become a container compatibility requirement.

## Base-image usage for an SPA

A frontend application may use Cerv as its final runtime stage:

```dockerfile
FROM node:24-bookworm-slim AS build
WORKDIR /app
COPY package*.json ./
RUN npm ci
COPY . .
RUN npm run build

FROM cerv:1.0.0
COPY --from=build /app/dist/ /srv/cerv/
ENV CERV_SPA_FALLBACK=/index.html
```

The resulting image contains the built static application and Cerv, not Node/npm/build dependencies.

SPA fallback is opt-in because a generic static origin should return real 404s. When enabled, only representation-level 404s are retried against the configured fallback file.

## Capacity in containers

Container runtimes inherit or configure process `RLIMIT_NOFILE`; the image cannot safely assume a particular value. `CERV_MAX_CONNECTIONS=auto` therefore remains the image default. It caps the 4096 service-wide target to the current worker and FD environment so the default configuration remains valid under ordinary container limits.

Operators who require an exact capacity use a numeric value and configure the runtime `nofile` limit accordingly. Cerv continues to fail startup rather than silently violating an explicit numeric capacity request.

## Verification

`make container-check` builds the image with Docker or Podman, starts it with `nofile=1024`, uses only native environment configuration, verifies a deep SPA route resolves through the fallback, and stops the container through SIGTERM. The target is intentionally not hidden inside the ordinary compiler-only test gate because a container engine is an external integration dependency.
