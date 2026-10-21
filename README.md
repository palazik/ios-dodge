# ios-dodge

**Porting Apple's open-source XNU (iOS 16 / xnu-8796) to the OnePlus 13 (`dodge`, Qualcomm SM8750 / Oryon).**

This is a **fork of [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet)**, which first
got iOS 16 + XNU booting on a Qualcomm phone (Redmi Note 10 Pro, `sweet`, SM7150). All credit for the
method — build XNU from source, add a non-Apple board config reusing XNU's built-in GICv3 + open pmap,
run the unmodified iOS userland — goes to dipa-studio. This fork adapts that method to a different device.

### Credits
- **[dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet)** — the method and the proven
  patch series this fork builds on (kept intact under `xnu/patches/`, `NOTES-sweet-upstream.md`,
  `AGENTS-sweet-upstream.md`, `README-sweet-upstream.md`). We reuse and adapt its patches for
  `dodge`, with credit.
- **[MakrSas/ios-topaz](https://github.com/MakrSas/ios-topaz)** — *another fork of ios-sweet*
  (Redmi Note 12), so a sibling of this project (both descend from dipa-studio). Its notes flagged a
  dangerous failure mode (an iOS UFS driver that wrote to LUN0 and destroyed its GPT); those safety
  rules are captured in **`RECOVERY.md`** and must be followed on `dodge`.
- **[UniXNU/UniXNU](https://github.com/UniXNU/UniXNU)** — runs iOS and macOS on non-Apple ARM64 via
  UEFI, and had already solved 4 KiB XNU before we needed it. **BSD-2-Clause-Patent**, so its patches
  are reused under licence rather than reimplemented; they apply to our tree unmodified, both being
  xnu-8796.141.3. We take `native-4k-bootstrap-capacity`, `native-4k-user-abi` and
  `native-4k-signature-hash-cache` (patches `0039`-`0040`). Their
  [four-k-page-tables](https://unixnu.com/xnu/four-k-page-tables) notes are the reference for this
  port's page-size work.
- **[blahajcoding/ios-lemonadep](https://github.com/blahajcoding/ios-lemonadep)** — ios-sweet on a
  OnePlus 9 Pro, and the furthest along of the sibling forks (XNU to PID 1 with launchd). Patch
  `0034` is theirs twice over: quiescing CNTV before handover, and not touching `CNTP_CTL_EL0`,
  which EL1 may not own. Their notes also supplied the framebuffer-alias warning recorded in
  `NOTES.md`.
- **[KSN2redawew/ios-giuliac](https://github.com/KSN2redawew/ios-giuliac)** — a fork of this
  repository for a OnePlus Ace 5. Found the 32 MB `PHYS_BASE` alignment bug that made our kernel's
  own KVA resolve to the wrong place, and the local-TLB-flush fix in `0029`. On-screen fault
  reporting (`0028`) and colour-coded progress stripes (`0030`) are their ideas.

> Status: **XNU boots far enough to switch on the MMU and survive it.** Built from source for
> `dodge` (plain arm64, no PAC, `NO_MONITOR`, GICv3, **4 KiB pages** — the SM8750's Oryon cores do
> not implement the 16 KiB translation granule, measured on both core types). Nothing boots to iOS
> yet. **Before any on-device test, read `RECOVERY.md`.**

## Layout
- **`dodge/`** — our OnePlus 13 work (new):
  - `dodge/patches/` — the XNU patches the CI builds, in order:
    - `0001` — `DODGE` board config: plain arm64 (no PAC), `NO_MONITOR` open pmap, GICv3, 16K
      (GICv3 dist 0x16000000 / redist 0x16080000 stride 0x40000, 19.2 MHz timer, vtimer INTID 27).
    - `0002` — open-build fixes (proc_reg DODGE case, pmap `reserved10` rename, legacy IOCPU+PSCI
      path, SART/UAT IOMMU header stubs).
    - `0003` — `NO_MONITOR` pmap page-range lockdown helpers + a forward decl.
    - `0004` — no-op `APPLY_TUNABLES` + `ptep_get_iommu` stub.
    - `0005` — closed-lib osfmk gaps: `amcc_rorgn`/`kpc` stubs, cache asm labels,
      `ml_enable_monitor`, `pmap_pending_preemption`, `SleepToken` guard.
    - `0006` — drop apple-SoC-only KPI exports (DART/SART/UAT/NVMe-PPL/`pmap_iommu`).
- **`RECOVERY.md`** — on-device safety & recovery (read before testing). **`NOTES.md`** — the active
  dodge handoff; **`AGENTS.md`** — how to work here (non-destructive policy).
- **`xnu/patches/`** — dipa-studio's original `sweet` patch series, **kept intact as the reference** we
  adapt device drivers from (GICv3 timer, PSCI SMP, UFS/HFS root, HID, display, …). Not applied to `dodge`.
- `shim/`, `tools/` — dipa-studio's loader/shim and tooling; adapted for `dodge` as we go.
- `README-sweet-upstream.md` — dipa-studio's original README.

## Build (CI)
`gh workflow run xnu.yml -R palazik/ios-dodge` → builds `xnu-8796.141.3` with `MACHINE_CONFIG=DODGE`
on a macOS runner via blacktop/darwin-xnu-build, applying `dodge/patches/`. Artifact: `xnu-kernel`.

## Device (OnePlus 13 / SM8750, from the live device tree)
GICv3 `arm,gic-v3` (dist 0x16000000, redist 0x16080000, stride 0x40000); ARM architected timer 19.2 MHz
(virtual = PPI 11 = INTID 27); 8 Oryon cores (2 clusters); 16K translation granule.
