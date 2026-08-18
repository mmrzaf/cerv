#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
version=$(cat "$root/VERSION")
epoch=${SOURCE_DATE_EPOCH:-$(cat "$root/RELEASE_EPOCH")}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

copy_source() {
  local dest=$1
  mkdir -p "$dest"
  tar -C "$root" --exclude='./build' --exclude='./dist' --exclude='./.git' -cf - . | tar -C "$dest" -xf -
}

for n in 1 2; do
  d="$work/build$n"
  copy_source "$d"
  (
    cd "$d"
    env -i PATH="$PATH" HOME="${HOME:-/tmp}" LC_ALL=C TZ=UTC SOURCE_DATE_EPOCH="$epoch" \
      make -s RELEASE_CC="${RELEASE_CC:-gcc}" release-bundle
  )
done

profile=linux-$(uname -m)
case "$profile" in
  linux-x86_64|linux-aarch64) ;;
  *) echo "unsupported reproducibility profile: $profile" >&2; exit 1 ;;
esac
for artifact in "cerv-$version-source.tar.gz" "cerv-$version-$profile.tar.gz" "cerv-$version-SHA256SUMS"; do
  a="$work/build1/dist/$artifact"
  b="$work/build2/dist/$artifact"
  sha1=$(sha256sum "$a" | awk '{print $1}')
  sha2=$(sha256sum "$b" | awk '{print $1}')
  if ! cmp -s "$a" "$b"; then
    echo "FAIL: controlled release artifact differs: $artifact $sha1 vs $sha2" >&2
    exit 1
  fi
  printf 'ok: reproducible %s sha256=%s\n' "$artifact" "$sha1"
done
printf 'ok: controlled Cerv %s release bundles reproduce bit-for-bit with SOURCE_DATE_EPOCH=%s\n' "$version" "$epoch"
