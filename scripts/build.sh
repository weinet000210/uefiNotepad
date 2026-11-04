#!/usr/bin/env bash
# Build uefinotepad.efi (X64) with EDK2 + GCC.
#   EDK2_DIR   path to an edk2 checkout (default: ../edk2 next to this repo)
#   TARGET     DEBUG | RELEASE (default RELEASE)
# On a non-x86 host, install x86_64-linux-gnu-gcc; the prefix is set automatically.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
EDK2_DIR="${EDK2_DIR:-$REPO/../edk2}"
TARGET="${TARGET:-RELEASE}"

if [ "$(uname -m)" != "x86_64" ]; then
  export GCC5_BIN=x86_64-linux-gnu-
fi

export WORKSPACE="$REPO/Build-ws"
export PACKAGES_PATH="$EDK2_DIR:$REPO"
mkdir -p "$WORKSPACE"
cd "$EDK2_DIR"
set +u
source edksetup.sh BaseTools >/dev/null
set -u
build -p UefiNotepadPkg/UefiNotepadPkg.dsc -a X64 -t GCC5 -b "$TARGET" -n "$(nproc)"
OUT="$WORKSPACE/Build/UefiNotepadPkg/${TARGET}_GCC5/X64/uefinotepad.efi"
mkdir -p "$REPO/dist"
cp "$OUT" "$REPO/dist/uefinotepad.efi"
mkdir -p "$REPO/dist/drivers"
cp "$WORKSPACE/Build/UefiNotepadPkg/${TARGET}_GCC5/X64/UsbMouseDxe.efi" "$REPO/dist/drivers/"
cp "$WORKSPACE/Build/UefiNotepadPkg/${TARGET}_GCC5/X64/UsbMouseAbsolutePointerDxe.efi" "$REPO/dist/drivers/"
echo "Built: $REPO/dist/uefinotepad.efi"
