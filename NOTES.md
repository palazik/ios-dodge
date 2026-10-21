# iOS 16 (xnu-8796) on dodge (OnePlus 13, SM8750 / Oryon) — working notes

Fork of dipa-studio/ios-sweet. We follow their PROVEN method (they reached the
SpringBoard home screen on sweet / Redmi Note 10 Pro, SM7150). Their full bring-up
log is `NOTES-sweet-upstream.md` — READ IT; it is the step-by-step recipe. Copy no
code (no license); write our own, credit dipa.

## Agent handoff / active work (2026-10-20)

**STATE: 🎉 FULL KERNEL FOUNDATION BUILDS — dodge patches 0001-0016 all green on CI.**
`kernel.development.dodge` is a plain-arm64 (no PAC) NOUNDEFS|PIE Mach-O. Foundation =
board cfg + open-build fixes (0001-0006), fb console (0007), physmap (0008), kmem-RNG
(0009), HMAC-PRNG (0010), trust-cache (0011), DodgePlatformExpert (0012), GICv3 Group-1
vtimer IRQ (0013), DodgeCPU boot-CPU (0014), built-in libpthread (0015), BearSSL AES (0016).
CI auto-triggers on push to dodge/patches/**; ccache warm.

**NEXT = QEMU RUNTIME VALIDATION before the userland tail.** Everything is compile-only
so far. With platform-expert + Group-1 IRQ + boot-CPU in place the kernel should now reach
the scheduler/console. Run the dodge qemu-dodge.py flow (docs/QEMU_TESTING.md; needs
research/ipsw-x/d22ap.adt from the iPhone X IPSW). THEN port dipa's userland tail (0021+:
UFS root [+ topaz 0088 gpt-watchdog], HFS, AMFI, HID, IOSurface/IOMFB, SMP/PSCI)
incrementally, validating each against that boot — do NOT blind-port ~60 patches.
Also pending: early-fb-debug (dipa 0010/0012) once the loader framebuffer geometry is known.

PORTING PROGRESS (reuse+adapt dipa/topaz, credited):
- 0008 physmap alignment; 0009 kmem RNG (SplitMix64); 0010 HMAC-SHA256 PRNG; 0011 trust-cache
  unavailable-runtime; 0012 DodgePlatformExpert; 0013 GICv3 Group-1 NS vtimer IRQ (TrustZone owns
  Group 0 -> timer/IPIs as IRQ via sleh_irq/ICC_IAR1; DODGE.h GIC macros, EOIMODE fix); 0014 DodgeCPU
  (single-core IOCPU, SGI1 self-test); 0015 built-in libpthread (staging .sh + provider); 0016 BearSSL AES
  (staging .sh + dodge_aes*.c). vtimer INTID 27 verified from DTS.
- 0007 framebuffer console DONE (build 9 run 39290737491 SUCCESS) = dipa 0003 ported: SERIAL_DODGE_FBCON,
  kprintf drawn on the panel via ADT node "dodge-fbcon" (reg/width/height/stride from the loader). OP13
  has no kernel UART, so the panel is the debug channel.
- DEVICE ADDRESS MAP (verified from work/sm8750/sm8750.dts + iomem.txt — use these, NOT sweet's SM7150):
  * UFS `ufshc@1d84000` (SAME base as sweet; Qualcomm reuse). ufs_mem 0x1d84000/0x3000, ICE 0x1d88000,
    mcq_* present (SM8750=UFS4/MCQ; port dipa's LEGACY UTRL path for bring-up). IRQ GIC_SPI 0x109.
  * Framebuffer (ABL cont-splash): base 0xfc800000 size 0x2b00000 (sweet was 0x9c000000). GEOMETRY not in
    the DTS and ABL-splash geometry != panel timing -> do NOT hardcode sweet's 1080x2400/4320.
  * GENI I2C qupv3_i2c_geni_se@9c0000/i2c@990000 (touch, later). SPMI qcom,spmi@c42d000 (buttons, later).
- NEXT PORT, DEFERRED for correctness: dipa 0010 early-fb-debug + 0012 keep-console-after-ttbr0. dipa
  HARDCODES sweet's fb geometry (EARLY_WIDTH 1080/HEIGHT 2400/STRIDE 4320) — wrong for dodge. Port when
  the loader is wired, reading geometry from boot_args.Video (the Silicium GOP) instead of hardcoding;
  the start.s stage-0 asm band should write a geometry-independent run at fb base 0xfc800000. Until then,
  the 0007 ADT-driven fbcon is our console once serial_init runs.
- Portable (device-agnostic) patches safe to port anytime next: 0011 physmap-alignment, 0013/0014 crypto
  builtins (kmem rng + hmac prng, since we don't link corecrypto), 0015 trust-cache runtime, 0019 builtin
  pthread, 0020 portable aes-cbc.

History of the build-enabling iteration (patches 0001-0006):
- Local xnu checkout we author in: `~/ios18_op13/work/ios16/xnu/` (git, author
  palazik <gochmyradov0@gmail.com>). Patches exported to
  `~/ios18_op13/work/ios16/patches/` and copied into `dodge/patches/`.
- Patches so far:
  - 0001 DODGE board config (makedefs/MakeInc.def machine config = plain arm64,
    arm64e force guarded; pexpert/arm64/DODGE.h = NO_MONITOR, HAS_GIC_V3,
    __ARM_16K_PG__, GICR stride 0x40000, DODGE_VTIMER_INTID 27; board_config.h DODGE
    block MAX_CPUS 8 / 2 clusters; config/MASTER.arm64.dodge = MASTER.arm64.MacOSX
    + nos_arm_asm nos_arm_pmap).
  - 0002 open-build fixes (proc_reg.h DODGE shares VMAPPLE cache-line branch; pmap.h
    reserved10->reserved10b; dropped USE_APPLEARMSMP = legacy IOCPU+PSCI path;
    osfmk/arm64/ppl/{sart,uat}.h no-op stubs).
  - 0003 NO_MONITOR pmap lockdown helpers + fwd decl of pmap_phys_write_disable.
  - 0004 tunables.s no-op APPLY_TUNABLES + ptep_get_iommu NULL stub.
  - 0005 closed-lib osfmk gaps: stub osfmk/arm64/{amcc_rorgn_common,amcc_rorgn_ppl}.c,
    a real osfmk/arm64/kpc.c backend (no counters; shape from <kern/kpc.h>),
    caches_asm.s (clean_mmu_dcache label + CleanPoC_DcacheRegion_Force_nopreempt{,_nohid}
    for !APPLE_ARM64_ARCH_FAMILY), ml_enable_monitor() no-op (#if DODGE),
    pmap_pending_preemption() -> _pmap_pending_preemption_real(), SleepToken under
    WITH_CLASSIC_S2R. Fixes build 6's "No rule to make target amcc_rorgn_*/kpc.c".
  - 0006 drop apple-SoC-only KPI exports (config/Private{,.arm64}.exports): the
    DART/SART/UAT/NVMe-PPL/pmap_iommu/IOUnifiedAddressTranslator/pmap_cs symbols the
    kernel doesn't define. Fixes build 7's check_all_exports / Private.symbolset Error 1.
- CI: `gh workflow run xnu.yml -R palazik/ios-dodge -f kernel_config=DEVELOPMENT -f machine_config=DODGE -f keep_going=true`
  (fork push events do NOT auto-trigger). build 7 (39261994284) compiled all osfmk+bsd,
  failed at export check -> 0006; build 8 (39264404384) = SUCCESS, kernel produced.
  `gh run view <id> --log-failed` can return EMPTY — use `--log` (full) + grep instead.

## Key lessons from dipa's NOTES that change the dodge plan (apply when we boot)
- **GIC timer is NOT a simple INTID-27 FIQ.** XNU's VMAPPLE GICv3 path (pexpert/arm/
  pe_fiq.c) puts the arch timer on PPI 27, **Group 0 = FIQ**. If the GIC has
  SecurityExtn=1 (DS=0) — true on sweet, CHECK on dodge — Group 0 is *secure* and
  undeliverable from NS-EL1. dipa reworked it to a **Group 1 IRQ**: sleh_irq acks
  ICC_IAR1, routes the vtimer INTID to rtclock_intr, EOI via ICC_EOIR1, enable via
  ICC_IGRPEN1. This is our equivalent of dipa 0017/0018 (device-tree vtimer IRQ +
  boot-CPU driver).
- **vtimer INTID = 27, VERIFIED (2026-10-19).** OP13 DTS timer node
  `interrupts = <1 13 ..  1 14 ..  1 11 ..  1 10 ..>` = sec-phys/phys/**virt**/hyp PPIs,
  so virt = PPI 11 -> GICv3 INTID 16+11 = 27 (matches DODGE.h; sweet was PPI 3 = 19).
  clock-frequency 0x124f800 = 19.2 MHz confirmed. Still CHECK GICD_TYPER SecurityExtn
  on dodge for the Group0-FIQ-vs-Group1-IRQ decision above.
- **PSCI conduit = SMC on Qualcomm** (VMAPPLE uses HVC). CPU_ON via SMC for SMP.
- **Debug channel = framebuffer**, likely no usable serial (sweet UART was SPI mode).
  Need an early fb console + markers like dipa 0003 (sweet-fbcon) / 0010 (early fb
  debug: PA marker band in start.s before MMU, then VA-alias putc through arm_init).
- **Machine config** (validates ours): VMAPPLE (16K, GICv3 pe_fiq.c) minus
  APPLEVIRTUALPLATFORM paravirt/HVC, plus BCM2837-style generic ARM (no
  APPLE_ARM64_ARCH_FAMILY), plain arm64 (no PAC), don't link the KDK lib.
- **SMP open question:** dipa's early plan used USE_APPLEARMSMP + built-in IOKit
  SweetGIC/SweetPMGR; we currently DROPPED USE_APPLEARMSMP (patch 0002, legacy path).
  Revisit at the SMP milestone — pick whichever reaches multi-core first.

## Known sibling-port BUGS to NOT reintroduce in dodge (read before porting those)
Credit/source: MakrSas/ios-topaz (Redmi Note 12) + dipa-studio/ios-sweet. See RECOVERY.md.
- **UFS driver destroyed LUN0's GPT (topaz).** When we port dipa's UFS/root patches (sweet
  0021-0034: mockfs ramdisk root, ufs-root-device, ufs-writes, read-write-root, ufs-dma, ufs-faster),
  the dodge storage driver MUST be safe by construction:
  * NON-DESTRUCTIVE is a project rule — the iOS root is a FILE / ramdisk / dedicated image, NEVER a
    raw Android partition, and the driver must NEVER touch LUN0's GPT (first 34 + last 33 sectors) or
    any partition table.
  * Bring-up is READ-ONLY: boot WITHOUT `ufsrw=1 rootrw=1`. Do not enable writes until the driver is
    audited and bounds-checked.
  * When writes are finally enabled, clamp every write to the declared root extent (reject any LBA
    outside it); never issue a write that can reach sector 0 / the backup GPT of any LUN.
  * dipa's console-log-to-userdata trick (sweet 0047: writes the kernel log into the LAST blocks of
    userdata) is a WRITE to a real partition — do NOT port it as-is; use RAM/UART/QEMU log instead.
  * REUSE, don't reinvent (user 2026-10-19): at the storage stage port dipa's UFS driver
    (sweet 0028 ufs-root-device, 0030 ufs-writes, 0031 read-write-root, 0034 ufs-dma) AND
    **topaz's `0088-topaz-gpt-watchdog.patch`** (MakrSas's fix added AFTER the GPT loss), adapted to
    dodge and credited. NOTE dipa's 0030 already "re-checks partition bounds before every write" yet
    topaz still lost its GPT -> the bounds check alone is INSUFFICIENT; the gpt-watchdog is the real
    guard. Reuse their patches + credit (user's call 2026-10-19; don't fuss over licenses).
- **fastboot rules (RECOVERY.md):** never `fastboot flash partition`/`erase`/`format`; `fastboot boot`
  only; back up LUN0 GPT before every test.

## QEMU (Arch Linux testbed — see ~/ios18_op13/ios16-arch-qemu-prompt.txt)
dipa already built a QEMU harness (in `tools/`, study + adapt for dodge, don't copy):
- `tools/mkadt.py OUT.adt --qemu` = QEMU-virt ADT: GICD 0x08000000, GICR 0x080a0000,
  CNTFRQ 62.5 MHz.
- `tools/qemu-sweet.py PAYLOAD OUTDIR [--secs N] [--gdb]` = host-side loader (segments
  + ADT + boot_args at their PAs + a 6-insn stub that sets x0 and jumps), runs
  `-M virt,gic-version=3 -cpu cortex-a76`, 4 GB RAM (so the fb and kernel PAs are plain
  RAM), outputs fb.png / regs.txt / qemu.log (ESR/FAR/ELR), `--gdb` = stub on :1234.
- Not emulated: Qualcomm XPUs, ABL, real panel, PSCI conduit (virt uses hvc). This is
  the fast iterate loop BEFORE touching the phone.

## Two boot targets (do not conflate)
1. QEMU on Arch Linux = dev testbed (above).
2. OnePlus 13 phone via Project Silicium UEFI = real-hardware deploy. HARD CONSTRAINT:
   NON-DESTRUCTIVE — never wipe userdata, never delete Android. `fastboot boot` (RAM
   only); if a flash is ever needed use the inactive A/B slot; iOS root is a FILE/
   ramdisk, never over an Android partition. ARB1 does not gate the custom boot image
   (only the Qualcomm signed firmware chain), bootloader must be UNLOCKED. (This
   differs from sweet's AGENTS, which allowed deleting Android — ignore that here.)

## Device facts (from work/sm8750 DTS + iomem; verify on live DT)
- GICv3: GICD 0x16000000 (0x10000), GICR 0x16080000 (0x200000), redistributor-stride
  0x40000, 1 region. Arch timer 19.2 MHz. CPUs: 8 Oryon / 2 clusters. 16K granule.
  Framebuffer bootloader-provided (ABL/Silicium), address via ADT/boot-args.

## SM8750 (Snapdragon 8 Elite) does not implement the 16K translation granule

Measured on the device, not inferred. The shim reads ID_AA64MMFR0_EL1 on the
boot core and prints it before handover:

    CPU    el=00000001  tg16=00000000
    MMFR0  2100122100001024

Decoded: PARange 44-bit, ASID 16-bit, TGran4 **supported**, TGran64
**supported**, TGran16 **NOT supported** (and TGran16_2 not supported either).
The convention is inverted between these fields: for TGran4/TGran64 a zero
means supported, for TGran16 a zero means not implemented.

xnu is built for 16K pages only. With TGran16 unimplemented, TCR_EL1.TG0/TG1
hold a reserved value, so every translation faults the instant SCTLR_EL1.M is
set. This is the hard wall at boot-progress stage 6: the kernel programs TCR,
TTBR0, TTBR1 and MAIR correctly and dies on the MMU enable itself. No amount of
fixing our own code gets past it on such a core.

hackiosx.dev's compatibility page does not mention page-size support, because
every chip tested so far implements 16K. For Snapdragon 8 Elite and anything
else using Qualcomm's Oryon cores, it is the first thing to check, and it is
one register read in the shim.

Two ways out:
  - boot xnu on a core that does implement 16K, if the prime cluster differs
    from the performance cluster (being measured; a big.LITTLE SoC is
    architecturally allowed to differ here);
  - rebuild xnu for 4K pages. That is not only ARM_PGSHIFT: with 4K, T0SZ/T1SZ
    become 25 (39-bit), and the kernel links at 0xfffffe0006000000 which only
    exists in a 47-bit TTBR1 region, so VM_KERNEL_LINK_ADDRESS, the ADT and
    mkpayload.py's PAGE constant all move together.
