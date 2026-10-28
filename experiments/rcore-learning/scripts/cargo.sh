#!/usr/bin/env bash
set -euo pipefail

learning_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
rustc_binary=$(rustup which --toolchain stable rustc)
cargo_binary=$(rustup which --toolchain stable cargo)
expected_version='rustc 1.98.1 (48a229cea 2026-09-01)'
actual_version=$("$rustc_binary" --version)
if [[ "$actual_version" != "$expected_version" ]]; then
    printf 'Expected %s; found %s. See ENVIRONMENT.md before changing the compiler.\n' \
        "$expected_version" "$actual_version" >&2
    exit 1
fi

# Keep registry downloads here and avoid the implicit installers in upstream make.
export CARGO_HOME="$learning_root/.cache/cargo"
export RUSTC="$rustc_binary"
export RUSTDOC="$(dirname -- "$rustc_binary")/rustdoc"
exec "$cargo_binary" "$@"
