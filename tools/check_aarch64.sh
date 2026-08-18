#!/usr/bin/env bash
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=$(cat "$ROOT/VERSION")
CLANG=${CLANG:-clang}
A64_CC=${A64_CC:-aarch64-linux-gnu-gcc}

command -v "$CLANG" >/dev/null 2>&1 || { echo "ERROR: clang is required for AArch64 policy validation" >&2; exit 1; }

A64_INCLUDE=${A64_INCLUDE:-}
if [[ -z "$A64_INCLUDE" ]]; then
    for candidate in /usr/aarch64-linux-gnu/include /usr/include/aarch64-linux-gnu; do
        if [[ -d "$candidate" ]]; then
            A64_INCLUDE=$candidate
            break
        fi
    done
fi
if [[ -z "$A64_INCLUDE" || ! -d "$A64_INCLUDE" ]]; then
    if [[ ${CERV_REQUIRE_FULL_AARCH64:-0} == 1 ]]; then
        echo "ERROR: AArch64 Linux UAPI headers not found; install libc6-dev-arm64-cross or set A64_INCLUDE" >&2
        exit 1
    fi
    echo "SKIP: AArch64 Linux UAPI headers are not installed; native ARM64 CI covers the release build"
    exit 0
fi
RESOURCE=$($CLANG -print-resource-dir)/include

# This is the exact production seccomp translation unit, compiled as AArch64
# against the installed AArch64 Linux UAPI.  It validates audit architecture,
# BPF structure layout usage, and the architecture-specific syscall vocabulary
# without substituting host x86 syscall numbers.
"$CLANG" --target=aarch64-linux-gnu -ffreestanding -nostdinc -I"$ROOT/src" \
    -isystem "$RESOURCE" -isystem "$A64_INCLUDE" -isystem /usr/include \
    -std=c17 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion \
    -Wshadow -Wformat=2 -Wundef -Wstrict-prototypes -Wmissing-prototypes \
    -Wcast-qual -Wwrite-strings -Wvla -Wswitch-enum -fno-common \
    -fsyntax-only "$ROOT/src/linux/seccomp_filters.c"
echo "ok: exact production seccomp policy compiles against AArch64 Linux UAPI"

if command -v "$A64_CC" >/dev/null 2>&1; then
    mkdir -p "$ROOT/build/cross-aarch64"
    mapfile -t PROD_SRCS < <(find "$ROOT/src" -type f -name '*.c' | sort)
    "$A64_CC" -I"$ROOT/src" -DCERV_VERSION=\"$VERSION\" -std=c17 -Wall -Wextra -Wpedantic -Werror -Wconversion \
        -Wsign-conversion -Wshadow -Wformat=2 -Wundef -Wstrict-prototypes \
        -Wmissing-prototypes -Wcast-qual -Wwrite-strings -Wvla -Wswitch-enum -fno-common \
        -O2 -DNDEBUG -fPIE -fstack-protector-strong -D_FORTIFY_SOURCE=3 \
        "${PROD_SRCS[@]}" -pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack \
        -o "$ROOT/build/cross-aarch64/cerv"
    echo "ok: full production tree cross-linked for AArch64"
else
    if [[ ${CERV_REQUIRE_FULL_AARCH64:-0} == 1 ]]; then
        echo "ERROR: a full AArch64 glibc cross-compiler is required" >&2
        exit 1
    fi
    echo "SKIP: full AArch64 glibc cross-compiler is not installed; exact policy/UAPI cross-check completed"
fi
