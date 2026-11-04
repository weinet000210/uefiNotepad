#!/usr/bin/env bash
# Try dist/uefinotepad.efi in QEMU + OVMF (q35) from the UEFI Shell.
# Needs qemu-system-x86 and ovmf. OVMF has no USB mouse driver, so the
# startup script loads the bundled UsbMouseDxe.efi first.
#   OVMF_CODE / OVMF_VARS can override the firmware paths.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OVMF_CODE="${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
OVMF_VARS="${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}"
TMP="$(mktemp -d)"
mkdir -p "$TMP/esp"
cp "$REPO/dist/uefinotepad.efi" "$REPO"/dist/drivers/*.efi "$TMP/esp/"
printf 'Hello from uefiNotepad!\r\nEdit me and press Ctrl+S.\r\n' > "$TMP/esp/hello.txt"
printf 'fs0:\r\nload UsbMouseDxe.efi\r\nconnect -r\r\nuefinotepad hello.txt\r\n' > "$TMP/esp/startup.nsh"
cp "$OVMF_VARS" "$TMP/vars.fd"
exec qemu-system-x86_64 -machine q35 -m 512 -cpu max \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
  -drive if=pflash,format=raw,file="$TMP/vars.fd" \
  -drive file=fat:rw:"$TMP/esp",format=raw \
  -device VGA,xres=1024,yres=768 \
  -device qemu-xhci -device usb-kbd -device usb-mouse \
  -net none "$@"
