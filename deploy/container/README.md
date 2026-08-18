# Container deployment

Cerv has an official multi-stage `Dockerfile` at the repository root. The runtime image:

- runs Cerv directly as PID 1 (no shell entrypoint/wrapper);
- runs as numeric non-root UID/GID `65532:65532`;
- listens on `0.0.0.0:8080` by default;
- serves `/srv/cerv` by default;
- uses `CERV_WORKERS=auto` and `CERV_MAX_CONNECTIONS=auto`;
- uses `SIGTERM` for graceful shutdown.

Build it locally:

```sh
docker build -t cerv:1.0.0 .
```

Serve a directory read-only:

```sh
docker run --rm \
  -p 8080:8080 \
  --read-only \
  --cap-drop=ALL \
  --security-opt=no-new-privileges \
  --ulimit nofile=65536:65536 \
  -v "$PWD/dist:/srv/cerv:ro" \
  cerv:1.0.0
```

`CERV_MAX_CONNECTIONS=auto` is intentionally the container default. It caps the 4096-slot target to what the current worker count and process `RLIMIT_NOFILE` can actually support. If you set a numeric `CERV_MAX_CONNECTIONS`, that number is a hard request and startup refuses it when the FD budget is insufficient.

## SPA runtime image

For a Vite/React/Vue-style application, deep client-side routes need an index fallback. Cerv keeps this opt-in:

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

`CERV_SPA_FALLBACK` retries the configured root-relative file only after a genuine representation 404. It does not replace 400/403/406/500-class outcomes, so filesystem/security/negotiation failures are not disguised as the SPA shell.

Do **not** set `CERV_IMMUTABLE=1` merely because the application is an SPA. That switch applies the immutable cache policy to the complete served tree, including `index.html`. Use it only when the complete deployment tree is genuinely immutable under your cache/release model.

## Environment variables

The runtime image relies on Cerv's native environment configuration; no shell script translates values into flags. CLI flags/ROOT override environment values.

Common container values:

```text
CERV_LISTEN=0.0.0.0:8080
CERV_ROOT=/srv/cerv
CERV_WORKERS=auto
CERV_MAX_CONNECTIONS=auto
CERV_SPA_FALLBACK=/index.html   # optional
```

The full environment-variable contract is in `docs/50-operations/00-cli-configuration.md`.

Container isolation is defense in depth. Cerv still applies its own `openat2()` path confinement, `no_new_privs`, process seccomp profiles, and Landlock when the outer runtime/kernel permits it.
