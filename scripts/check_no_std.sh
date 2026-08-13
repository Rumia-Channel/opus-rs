#!/usr/bin/env bash
# Verify opus-rs builds as `#![no_std]` across bare-metal / freestanding targets.
#
# Requires the targets to be installed, e.g.:
#   rustup target add thumbv7em-none-eabi wasm32-unknown-unknown
set -euo pipefail

cd "$(dirname "$0")/.."

TARGETS=(
    "thumbv7em-none-eabi"      # ARM Cortex-M (bare metal)
    "wasm32-unknown-unknown"   # WebAssembly (no WASI)
)

fail=0
for t in "${TARGETS[@]}"; do
    if rustup target list --installed 2>/dev/null | grep -q "$t"; then
        echo ":: building opus-rs (no_std + libm) for $t"
        if ! cargo build --no-default-features --features libm --target "$t"; then
            echo "!! FAILED: $t"
            fail=1
        fi
    else
        echo "-- skip $t (not installed; run: rustup target add $t)"
    fi
done

# Host no_std build (rlib, no panic handler needed for a library).
echo ":: building opus-rs (no_std + libm) for host"
cargo build --no-default-features --features libm || fail=1

exit $fail
