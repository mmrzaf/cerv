#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
version=$(cat "$root/VERSION")
epoch=${SOURCE_DATE_EPOCH:-$(cat "$root/RELEASE_EPOCH")}
out=${DIST_DIR:-$root/dist}
release_cc=${RELEASE_CC:-gcc}
arch=$(uname -m)
case "$arch" in
  x86_64) profile=linux-x86_64 ;;
  aarch64) profile=linux-aarch64 ;;
  *) echo "unsupported release architecture for named bundle: $arch" >&2; exit 1 ;;
esac
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$out" "$stage/source" "$stage/bin/cerv-$version-$profile"

# Deterministic source archive. Build products and VCS metadata are excluded.
tar -C "$root" --exclude='./build' --exclude='./dist' --exclude='./.git' \
  --exclude='*/__pycache__' --exclude='*.pyc' \
  --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$epoch" \
  -cf "$stage/cerv-$version-source.tar" .
gzip -n -9 < "$stage/cerv-$version-source.tar" > "$out/cerv-$version-source.tar.gz"
rm "$stage/cerv-$version-source.tar"

# Controlled release build using a declared compiler and deterministic environment.
(
  cd "$root"
  env LC_ALL=C TZ=UTC SOURCE_DATE_EPOCH="$epoch" make -s clean release RELEASE_CC="$release_cc"
)

bundle="$stage/bin/cerv-$version-$profile"
cp "$root/build/release/cerv" "$bundle/cerv.unstripped"
objcopy --only-keep-debug "$bundle/cerv.unstripped" "$bundle/cerv.debug"
cp "$bundle/cerv.unstripped" "$bundle/cerv"
strip --strip-debug "$bundle/cerv"
objcopy --add-gnu-debuglink="$bundle/cerv.debug" "$bundle/cerv"
rm "$bundle/cerv.unstripped"
cp "$root/LICENSE" "$root/README.md" "$root/CHANGELOG.md" "$root/SECURITY.md" "$root/VERSION" "$bundle/"
mkdir -p "$bundle/docs" "$bundle/deploy"
cp "$root/docs/50-operations/02-verification-contract.md" "$bundle/docs/"
cp "$root/docs/60-release/00-release-supply-chain.md" "$bundle/docs/"
cp -a "$root/deploy/." "$bundle/deploy/"

compiler_full=$($release_cc --version)
compiler=${compiler_full%%$'\n'*}
libc_full=$(ldd --version 2>&1)
libc=${libc_full%%$'\n'*}
SOURCE_DATE_EPOCH="$epoch" python3 "$root/tools/generate_sbom.py" \
  --version "$version" --source-root "$root" --binary "$bundle/cerv" \
  --compiler "$compiler" --libc "$libc" --output "$bundle/cerv.spdx.json"

cflags=$(make -s -C "$root" -f Makefile --eval='print-release-cflags:;@printf "%s" "$(RELEASE_CFLAGS)"' print-release-cflags)
# Provenance records the semantic remapping flag without leaking the ephemeral checkout path.
cflags=${cflags//$root/<SOURCE>}
ldflags=$(make -s -C "$root" -f Makefile --eval='print-release-ldflags:;@printf "%s" "$(RELEASE_LDFLAGS)"' print-release-ldflags)
python3 "$root/tools/generate_provenance.py" \
  --version "$version" --source-archive "$out/cerv-$version-source.tar.gz" --binary "$bundle/cerv" \
  --compiler "$compiler" --cflags "$cflags" --ldflags "$ldflags" --source-date-epoch "$epoch" \
  --output "$bundle/PROVENANCE-UNSIGNED.json"
cat > "$bundle/ATTESTATION-STATUS.txt" <<EOF
This local bundle contains deterministic build provenance but no cryptographic release signature.
Hosted release automation adds signed artifact attestations; consumers should verify the published hashes and attestations for hosted artifacts.
EOF

(
  cd "$bundle"
  find . -type f ! -name SHA256SUMS -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > SHA256SUMS
)

# Deterministic binary bundle.
tar -C "$stage/bin" --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$epoch" \
  -cf "$stage/cerv-$version-$profile.tar" "cerv-$version-$profile"
gzip -n -9 < "$stage/cerv-$version-$profile.tar" > "$out/cerv-$version-$profile.tar.gz"
rm "$stage/cerv-$version-$profile.tar"

(
  cd "$out"
  sha256sum "cerv-$version-source.tar.gz" "cerv-$version-$profile.tar.gz" > "cerv-$version-SHA256SUMS"
)
printf 'created %s\ncreated %s\n' "$out/cerv-$version-source.tar.gz" "$out/cerv-$version-$profile.tar.gz"
