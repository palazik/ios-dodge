#!/bin/bash
# Build the dodge shim (OnePlus 13) with clang/LLVM and wrap it as a boot image.
# Same shim as dipa's build.sh, but using the LLVM toolchain (no
# aarch64-linux-gnu-gcc needed) and the OP13 dtb + framebuffer.
#
# Usage: build-dodge.sh PAYLOAD [OUTDIR]
#   PAYLOAD  a DODGEXNU payload from tools/mkpayload.py (kernel + ADT + boot-args)
#
# No device tree is passed or bundled, unlike sweet's header-v2 flow. A header
# v4 boot image carries no dtb at all, and the shim does not need one baked in:
# ABL hands it the live FDT pointer in x0 (see start.S), which is the device's
# own, always current, and cannot go stale against the phone. The dtb dumped
# from /sys/firmware/fdt is still worth keeping on the PC - every address in
# tools/mkadt.py was read out of it - but nothing in the build consumes it.
#
# RAM-boot the result with:  fastboot boot OUTDIR/dodge-boot.img   (writes nothing)
set -euo pipefail
cd "$(dirname "$0")"

PAYLOAD="${1:?need a DODGEXNU payload (tools/mkpayload.py)}"
OUT="${2:-out-dodge}"
mkdir -p "$OUT"

TARGET=aarch64-linux-gnu
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra"
for f in start.S shim.c console.c mmu.c fdt.c loader.c ufs.c; do
	clang --target=$TARGET $CFLAGS -c "$f" -o "$OUT/${f%.*}.o"
done
# No absolute relocations in the C objects: the image runs wherever ABL loads it.
abs=$(llvm-readelf -r "$OUT"/shim.o "$OUT"/console.o "$OUT"/mmu.o "$OUT"/fdt.o "$OUT"/loader.o "$OUT"/ufs.o 2>/dev/null | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
clang --target=$TARGET $CFLAGS -static -fuse-ld=lld -T shim.ld \
	-Wl,--build-id=none -Wl,-N \
	"$OUT"/start.o "$OUT"/shim.o "$OUT"/console.o "$OUT"/mmu.o "$OUT"/fdt.o "$OUT"/loader.o "$OUT"/ufs.o \
	-o "$OUT/shim.elf"
llvm-objcopy -O binary "$OUT/shim.elf" "$OUT/Image"
# Pad to _end so .bss/stacks exist as zeroed bytes (ABL just copies the file).
size=$(llvm-nm "$OUT/shim.elf" | awk '$3=="_end"{print strtonum("0x"$1)}')
truncate -s "$size" "$OUT/Image"
# Append the payload directly after _end, so loader.c finds it self-relative.
# Do NOT rely on the ramdisk: a GKI / header-v4 boot makes ABL build its own
# initrd from vendor_boot+init_boot and drop ours (observed on the OP13 as
# "no DODGEXNU payload" with a 21 MB initrd that was not ours).
cat "$PAYLOAD" >> "$OUT/Image"
# Tiny empty ramdisk — some bootloaders dislike a zero-size one.
# Fixed mtime and gzip -n so the image is byte-reproducible: cpio records the
# directory's mtime and gzip would record its own, which made two builds of
# identical inputs differ and defeated comparing SHA256SUMS across rebuilds.
(cd "$OUT" && rm -rf rd && mkdir rd && touch -d @0 rd && cd rd && \
	find . | cpio -o -H newc --quiet --reproducible | gzip -9n > ../ramdisk.bin)

# header v4 — the OP13's native (Android 15 / GKI) format, confirmed booting on
# device. v4 stores no load addresses and no dtb, so ABL uses its own known-good
# ones and the device's own device tree; there is nothing left to guess.
mkbootimg --header_version 4 --kernel "$OUT/Image" --ramdisk "$OUT/ramdisk.bin" \
	--cmdline "console=null" -o "$OUT/dodge-boot.img"

ls -l "$OUT/Image" "$OUT/dodge-boot.img"
