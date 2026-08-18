#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 ELF" >&2
    exit 2
fi

elf=$1
header=$(readelf -W -h "$elf")
program=$(readelf -W -l "$elf")
dynamic=$(readelf -W -d "$elf")

printf '%s\n' "$header" | grep -Eq 'Type:[[:space:]]+DYN' || {
    echo "hardening check failed: not PIE (ELF type is not DYN)" >&2
    exit 1
}

if printf '%s\n' "$program" | grep -q 'INTERP'; then
    echo "hardening check failed: dynamic ELF interpreter present; release must be static PIE" >&2
    exit 1
fi

if printf '%s\n' "$dynamic" | grep -q '(NEEDED)'; then
    echo "hardening check failed: shared-library dependency present; release must be static PIE" >&2
    exit 1
fi

printf '%s\n' "$program" | grep -q 'GNU_RELRO' || {
    echo "hardening check failed: GNU_RELRO missing" >&2
    exit 1
}

printf '%s\n' "$dynamic" | grep -Eq 'BIND_NOW|FLAGS.*NOW' || {
    echo "hardening check failed: immediate binding missing" >&2
    exit 1
}

stack_line=$(printf '%s\n' "$program" | grep 'GNU_STACK' || true)
[ -n "$stack_line" ] || {
    echo "hardening check failed: GNU_STACK missing" >&2
    exit 1
}

printf '%s\n' "$stack_line" | grep -q 'E' && {
    echo "hardening check failed: executable stack" >&2
    exit 1
}

echo "ok: static PIE + RELRO + NOW + non-executable stack: $elf"
