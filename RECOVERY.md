# dodge (OnePlus 13) — on-device safety & recovery

Read this before RAM-booting anything on the phone. A sibling port
(MakrSas/ios-topaz, Redmi Note 12) had its iOS **UFS driver write to LUN0 and
destroy the GPT** — every LUN0 partition vanished and the bootloader could boot
nothing. These rules keep that from happening on dodge.

## Never do
- `fastboot flash partition ...` with any `gpt_*.bin` — writing a GPT wipes the
  partition layout.
- `fastboot erase <part>` or `fastboot format <part>`.
- Hand-edit GPT A/B attributes. Switch the active slot only with
  `fastboot set_active a|b`.
- Flash the test UEFI/kernel to the ACTIVE boot slot. RAM-boot it instead.
- Boot the iOS kernel with `ufsrw=1 rootrw=1` (or any write-enabling arg) until
  the dodge UFS driver is trusted — a bad write can corrupt LUN0's GPT.

## Always do
- RAM-boot only: `fastboot boot <image>` writes nothing to flash.
- Keep the bootloader UNLOCKED. (ARB1 is fine — Anti-Rollback gates only the
  Qualcomm signed firmware chain XBL/ABL/TZ, which we never flash.)
- Keep Android and userdata intact (project rule): the iOS root is a file/ramdisk,
  never written over an Android partition.

## Back up the LUN0 GPT before every iOS test
Needs root adb or a custom recovery (TWRP/OrangeFox) with `adb root`. LUN0 is
`/dev/block/sda`; the GPT lives in the first sectors (primary) and last sectors
(backup), so save the first and last 1 MiB:

```sh
# primary GPT (first 1 MiB)
adb exec-out "su -c 'dd if=/dev/block/sda bs=1M count=1'" > lun0-head-1MiB.bin
# backup GPT (last 1 MiB)
S=$(adb shell "su -c 'blockdev --getsz /dev/block/sda'" | tr -d '\r')
adb exec-out "su -c 'dd if=/dev/block/sda bs=512 skip=$((S-2048)) count=2048'" > lun0-tail-1MiB.bin
# human-readable map (if available)
adb shell "su -c 'sgdisk -p /dev/block/sda'" > lun0-sgdisk.txt 2>/dev/null || true
```
(The friend pack ships `gpt-backup.sh`, which does this and more.)

## Back up what cannot be downloaded again
`persist`, `modemst1`, `modemst2`, `fsg` and `fsc` hold calibration and radio
data that is unique to the individual handset. It is in no firmware package and
cannot be re-downloaded; losing it costs IMEI, sensor calibration and working
mobile data, permanently. `boot`, `dtbo` and `vbmeta` are re-flashable from
stock firmware but are worth having to hand.

`tools/gpt-backup.sh` saves all of these alongside the GPT, resolving the A/B
suffix automatically, and warns if a unique one could not be read. Keep the
output directory off the phone. (This list is the one hackiosx.dev recommends
for any Snapdragon port.)

## If the GPT/partitions are lost
- Restore from your dump, from the SAME recovery: `dd` the head dump back to
  `/dev/block/sda` offset 0 and the tail dump to the end — NOT with
  `fastboot flash partition` and NOT with a ROM `gpt_*.bin`.
- If only Android is damaged (GPT intact): re-flash the OnePlus 13 firmware with
  the OEM tool / MSMDownloadTool, which does not need our files.
- dodge-specific GPT/partition dumps should be stored off-device before testing;
  this repo intentionally keeps device dumps out of git.

## Boot-args for safe bring-up (no disk writes)
`debug=0xc serial=3 cpus=1 keepsyms=1 -v -noprogress`  — single Gold core, verbose,
NO `ufsrw`/`rootrw`, no `rd=ufsroot`. (16K pages: only Gold/big cores can run this
kernel; bring-up is single-core anyway. SMP later enables only 16K-capable cores.)
