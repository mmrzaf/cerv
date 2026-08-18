#!/usr/bin/env bash
set -euo pipefail

fail=0

layer_of() {
    local path=$1
    path=${path#src/}
    case "$path" in
        base/*) echo base ;;
        http/*) echo http ;;
        linux/*) echo linux ;;
        serve/*) echo serve ;;
        runtime/*) echo runtime ;;
        process/*) echo process ;;
        main.c) echo main ;;
        README.md|CONTRACTS.md) echo docs ;;
        *) echo unknown ;;
    esac
}

allowed_edge() {
    case "$1:$2" in
        base:base|http:base|http:http|linux:base|linux:linux|serve:base|serve:http|serve:linux|serve:serve|runtime:base|runtime:http|runtime:linux|runtime:serve|runtime:runtime|process:base|process:linux|process:runtime|process:process|main:process)
            return 0 ;;
        *) return 1 ;;
    esac
}

while IFS=: read -r file line include; do
    src_layer=$(layer_of "$file")
    dst="src/$include"
    if [[ ! -f "$dst" ]]; then
        printf 'ERROR: %s:%s includes unknown internal path %s\n' "$file" "$line" "$include" >&2
        fail=1
        continue
    fi
    dst_layer=$(layer_of "$dst")
    if ! allowed_edge "$src_layer" "$dst_layer"; then
        printf 'ERROR: forbidden source dependency %s -> %s at %s:%s\n' "$src_layer" "$dst_layer" "$file" "$line" >&2
        fail=1
    fi
done < <(
    grep -R -n -E '^#include "(base|http|linux|serve|runtime|process)/[^\"]+"' src --include='*.c' --include='*.h' |
    sed -E 's/^([^:]+):([0-9]+):#include "([^"]+)"$/\1:\2:\3/'
)

# Project headers inside src must be qualified by architectural directory.
while IFS=: read -r file line text; do
    include=${text#*\"}
    include=${include%%\"*}
    if [[ "$include" != */* ]]; then
        printf 'ERROR: unqualified quoted include %s at %s:%s\n' "$include" "$file" "$line" >&2
        fail=1
    fi
done < <(grep -R -n -E '^#include "[^"]+"' src --include='*.c' --include='*.h' || true)

if (( fail != 0 )); then
    exit 1
fi
printf 'ok: source dependency layers are one-way and includes are qualified\n'
