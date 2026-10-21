#!/usr/bin/env bash
# gpt-backup.sh — dump the OnePlus 13 (dodge) LUN0 partition table AND the
# partitions that cannot be downloaded again, BEFORE any iOS test.
#
# Two separate things are saved here:
#
# 1. The LUN0 (/dev/block/sda) GPT. A GPT lives in the FIRST sectors (primary)
#    and the LAST sectors (backup), so we save the first and last 1 MiB.
#
# 2. The per-phone partitions. persist, modemst1, modemst2, fsg and fsc hold
#    calibration and radio data that is UNIQUE TO THIS HANDSET: it is not in
#    any firmware package and cannot be re-downloaded. Losing it costs you
#    IMEI, sensor calibration and working mobile data, permanently. boot, dtbo
#    and vbmeta are re-flashable from stock firmware but are saved too, since
#    they are what you need to get back to a bootable phone in a hurry.
#    (This list is the one hackiosx.dev recommends for any Snapdragon port.)
#
# Needs root adb OR a custom recovery (TWRP/OrangeFox) with `adb root`.
# Pure-fastboot users can skip this — `fastboot boot` writes nothing — but a
# backup is strongly recommended before testing anything that touches storage.
#
# Usage:  ./gpt-backup.sh [OUTDIR]     (default OUTDIR: ./dodge-gpt-backup-<date>)
set -euo pipefail

OUT="${1:-dodge-gpt-backup-$(date +%Y%m%d-%H%M%S)}"
LUN=/dev/block/sda          # LUN0 on Qualcomm UFS
BYNAME=/dev/block/by-name
# Unique to this handset, unrecoverable if lost.
CRITICAL="persist modemst1 modemst2 fsg fsc"
# Re-flashable from stock firmware, but saved so a brick is a quick fix.
RECOVERABLE="boot dtbo vbmeta"
mkdir -p "$OUT"

if ! adb shell true >/dev/null 2>&1; then
  echo "ERROR: no adb device. Boot a rooted Android or a custom recovery first." >&2
  exit 1
fi

echo "Backing up LUN0 ($LUN) GPT to $OUT/ ..."
# Primary GPT: first 1 MiB (protective MBR + GPT header + entries).
adb exec-out "su -c 'dd if=$LUN bs=1M count=1'" > "$OUT/lun0-head-1MiB.bin"
# Backup GPT: last 1 MiB. Compute the device size in 512-byte sectors, seek to end-2048.
SECTORS=$(adb shell "su -c 'blockdev --getsz $LUN'" | tr -d '\r')
if [ -n "$SECTORS" ] && [ "$SECTORS" -gt 2048 ]; then
  SEEK=$(( SECTORS - 2048 ))
  adb exec-out "su -c 'dd if=$LUN bs=512 skip=$SEEK count=2048'" > "$OUT/lun0-tail-1MiB.bin"
else
  echo "WARN: could not read $LUN size; tail (backup GPT) NOT saved." >&2
fi

# Also save a human-readable partition map if sgdisk/parted exist on-device.
adb shell "su -c 'sgdisk -p $LUN'"  > "$OUT/lun0-sgdisk.txt" 2>/dev/null || \
adb shell "su -c 'parted $LUN unit s print'" > "$OUT/lun0-parted.txt" 2>/dev/null || \
echo "(no sgdisk/parted on device; raw dumps only)"

missing_critical=0
dump_partition() {
  name="$1"
  # Resolve by-name first; the A/B suffix varies, so try both.
  for cand in "$BYNAME/$name" "$BYNAME/${name}_a" "$BYNAME/${name}_b"; do
    if adb shell "su -c '[ -e $cand ]'" >/dev/null 2>&1; then
      out="$OUT/$(basename "$cand").img"
      echo "  $cand"
      adb exec-out "su -c 'dd if=$cand'" > "$out"
      [ -s "$out" ] || { echo "  WARN: $cand produced an empty file" >&2; rm -f "$out"; return 1; }
      return 0
    fi
  done
  return 1
}

echo "Backing up per-handset partitions (unique, NOT downloadable) ..."
for p in $CRITICAL; do
  dump_partition "$p" || { echo "  WARN: $p not found" >&2; missing_critical=1; }
done

echo "Backing up re-flashable partitions ..."
for p in $RECOVERABLE; do
  dump_partition "$p" || echo "  note: $p not found (skipped)" >&2
done

( cd "$OUT" && sha256sum ./* > SHA256SUMS 2>/dev/null || true )

echo
echo "Done. Keep $OUT/ somewhere safe and OFF the phone."
if [ "$missing_critical" = 1 ]; then
  echo "WARNING: at least one unique partition could not be saved. Check that su"
  echo "works and that $BYNAME exists before relying on this backup." >&2
fi
echo "To restore the GPT later, re-flash the dumps from the SAME recovery (dd"
echo "back to $LUN at offset 0 and at the tail), NOT with 'fastboot flash"
echo "partition' and NOT with a ROM gpt_*.bin."
