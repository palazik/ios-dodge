#!/usr/bin/env python3
"""Build the minimal Apple Device Tree that the DODGE xnu needs to boot.

Only what the early boot path actually reads (see NOTES.md); every MMIO
address comes from the dodge live FDT. Usage:  mkadt.py OUT.adt [--dump] [--qemu] [--highmem]
"""
import hashlib, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt  # noqa: E402

# Kernel-managed RAM: above every Qualcomm XPU carve-out (see NOTES.md)
# dodge: XNU-managed RAM must be a real DRAM bank with NO reserved-memory
# carve-out inside it (start.s maps physBase..physBase+memSize as cacheable
# blocks; a cacheable mapping over a Qualcomm XPU carve-out faults). Verified
# against the OP13 memory node + reserved-memory: the bank at 0xbe360000
# (size 0x16ac3000, ends 0xd4e23000) is carve-out free, and it is inside the
# shim's low-8GB identity map. sweet's 0xb0000000/0x4d000000 is NOT usable here
# (it spans xbl_ramdump, gunyah_hyp_region1, tz_merged, oem_vm and the splash fb).
#
# PHYS_BASE must also be congruent to the kernel's virtual base modulo the L2
# block size (32MB with 16K pages), and the simplest way is to make it
# 32MB-aligned, since the virtual base already is. start.s builds the bootstrap
# KVA map out of L2 blocks and create_l2_block_entries masks the physical
# address with ARM_TTE_BLOCK_L2_MASK (bits 47:25), so a block always maps
# block-base to block-base. With the old 0xbe360000 the virtual base had offset
# 0 inside its block and the physical base had offset 0x360000, so the KVA of
# the kernel resolved 0x360000 BELOW where the kernel actually sits, and xnu
# landed in garbage as soon as it branched to its own address space. The V=P
# map never showed this, because there VA and PA are trivially congruent.
#
# 0xc0000000..0xd4000000 is inside the same carve-out-free bank as before.
# Alignment found by KSN2redawew/ios-giuliac (commit 6dd49748), which hit it
# first on an Ace 5.
PHYS_BASE = 0xc0000000
MEM_SIZE = 0x14000000
# --highmem (phone only): the whole upper DDR bank. The live FDT memory node is
# <0x80000000 2G>, <0x1_00000000 0xfcc00000>; every fixed reserved-memory
# carve-out is in the low bank and the dynamic pools are limited below 4G.
HIGH_PHYS_BASE = 0x100000000
HIGH_MEM_SIZE = 0xfcc00000
# /arm-io children are relative to this; it must not be 0 (serial_init bails)
SOC_BASE = 0x01000000

# OnePlus 13 (dodge, SM8750) live FDT addresses (work/sm8750/sm8750.dts, iomem.txt)
GICD, GICD_SIZE = 0x16000000, 0x10000
GICR, GICR_SIZE = 0x16080000, 0x200000    # redistributor region (stride 0x40000, 1 region)
FB, FB_SIZE = 0xfc800000, 0x2b00000       # ABL cont_splash_region (phone)
DODGE_ECID = 0x444f444745000001  # 'DODGE' + 1, synthetic
# phone framebuffer geometry: TODO set from the real Silicium GOP; 1080x2400 placeholder
FB_W, FB_H, FB_STRIDE = 1440, 3168, 1440 * 4   # OP13 QHD+, confirmed on-device; match shim/console.h
# Internal UFS host (ufshc@1d84000 "ufs_mem"; Qualcomm reuses this base across SoCs); QEMU has none
UFS, UFS_SIZE = 0x01d84000, 0x3000
TIMEBASE = 19200000
VTIMER_INTID = 27                         # virt timer PPI 11 -> GICv3 INTID 27 (verified from DTS)
# QEMU virt (-M virt,gic-version=3 -cpu cortex-a76): GICD/GICR + CNTFRQ, and a synthetic
# framebuffer in plain RAM that tools/qemu-dodge.py captures (must match its FB_BASE/W/H).
QEMU_GICD, QEMU_GICR, QEMU_TIMEBASE = 0x08000000, 0x080a0000, 62500000
QEMU_VTIMER_INTID = 27
QEMU_FB, QEMU_FB_SIZE, QEMU_FB_W, QEMU_FB_H = 0x9c000000, 0x1700000, 1080, 2400

BOOT_MANIFEST_HASH = hashlib.sha384(b'dodge boot manifest').digest()

# The iOS userland is the iPhone X (iPhone10,3 / D22AP) IPSW. MobileGestalt
# derives product type, device class, home-button-less UI, artwork etc. from
# the root identity and /product of the real D22AP device tree. That tree is
# an Apple artifact (kept out of git): extract it from the IPSW with
#   tools/remotezip.py URL DeviceTree.d22ap.im4p DIR
#   ipsw img4 im4p extract DIR/Firmware/all_flash/DeviceTree.d22ap.im4p -o d22ap.adt
D22AP_ADT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../research/ipsw-x/d22ap.adt')


def d22ap_product(path=D22AP_ADT):
    """/product properties of the real D22AP tree, without the syscfg
    placeholders iBoot would fill in (flag bit set) and the node bookkeeping."""
    if not os.path.exists(path):
        print(f'warning: {path} missing, no /product node', file=sys.stderr)
        return None
    root, _ = adt.parse(open(path, 'rb').read())
    for c in root.children:
        if dict((k, v) for k, f, v in c.props).get('name') == b'product\0':
            return [(k, v) for k, f, v in c.props if not f and k not in ('name', 'AAPL,phandle')]
    raise SystemExit(f'{path}: no /product node')

u32 = lambda *v: struct.pack('<%dI' % len(v), *v)
u64 = lambda *v: struct.pack('<%dQ' % len(v), *v)
cstr = lambda s: s.encode() + b'\0'


def node(name, props=(), children=()):
    n = adt.Node()
    n.props = [('name', 0, cstr(name))] + [(k, 0, v) for k, v in props]
    n.children = list(children)
    return n


def build(seed=None, qemu=False, highmem=False):
    """qemu=True: same tree, but with QEMU virt's GIC addresses and CNTFRQ (tools/qemu-dodge.py)."""
    phys_base, mem_size = (HIGH_PHYS_BASE, HIGH_MEM_SIZE) if highmem else (PHYS_BASE, MEM_SIZE)
    gicd, gicr, timebase = (QEMU_GICD, QEMU_GICR, QEMU_TIMEBASE) if qemu else (GICD, GICR, TIMEBASE)
    fb, fb_size, fb_w, fb_h = (QEMU_FB, QEMU_FB_SIZE, QEMU_FB_W, QEMU_FB_H) if qemu else (FB, FB_SIZE, FB_W, FB_H)
    seed = seed if seed is not None else os.urandom(64)
    phandle = iter(range(1, 1000))
    ph = lambda: ('AAPL,phandle', u32(next(phandle)))

    chosen = node('chosen', [
        ph(),
        ('dram-base', u64(phys_base)),
        ('dram-size', u64(mem_size)),
        ('random-seed', seed),
        ('debug-enabled', u32(1)),
        ('firmware-version', cstr('dodge-shim')),
        ('system-firmware-version', cstr('dodge-shim')),
        # iBoot's SHA-384 of the boot manifest; /sbin/mount -P 1 exits unless
        # it exists (bind mounts /private/preboot/<hex>/usr/standalone/firmware,
        # tolerating a missing directory). Fixed stand-in value.
        ('boot-manifest-hash', BOOT_MANIFEST_HASH),
        # libimg4 (cryptexd) aborts "failed to get crypto algorithm" without
        # it; real A11+ device trees say sha2-384.
        ('crypto-hash-method', cstr('sha2-384')),
        # MobileGestalt's screen class: display-scale (u32, default 0) comes
        # from /chosen, the pixel size from IOMobileFramebuffer. Scale 0 makes
        # SpringBoard's FBSDisplayMode "infxinf@0x" invalid. 1080x2400 @3x =
        # 360x800 points (like iPhone X class devices).
        ('display-scale', u32(3)),
        # XNU derives IOPlatformUUID (gethostuuid) from these on arm; without
        # them CFPreferences cannot use kCFPreferencesCurrentHost (no ByHost
        # files), so e.g. DataMigration never records LastSystemVersion and
        # migrates on every boot. Synthetic ECID (not the phone's); chip-id
        # of the A11 that the iPhone X userland expects.
        ('unique-chip-id', struct.pack('<Q', DODGE_ECID)),
        ('chip-id', u32(0x8015)),
        # Security status as on a dev-fused, undemoted board (tuple 0,0,1,1):
        # MobileGestalt reads these from /chosen, and mobileactivationd's
        # DeviceType then takes its native "Hactivation" branch (state
        # Activated without an activation record; no SEP, no baseband here).
        ('certificate-production-status', u32(0)),
        ('effective-production-status-ap', u32(0)),
        ('certificate-security-mode', u32(1)),
        ('effective-security-mode-sep', u32(1)),
    ], [node('memory-map', [ph()]),
        # Local boot policy (kern_csr.c): SIP relaxed like `csrutil disable`
        # (lp-sip0: untrusted kexts, unrestricted FS, task_for_pid, kernel
        # debugger, dtrace, nvram), lp-sip1: unauthenticated (unsealed) root.
        # The iOS shared cache is in no trust cache, so dyld can only map it
        # with CSR_ALLOW_UNRESTRICTED_FS (vm_unix.c shared region checks).
        node('asmb', [ph(), ('lp-sip0', u64(0x6f)), ('lp-sip1', u32(1))])])

    # DODGE SMP: all eight cores in physical-core order. The OnePlus 13 is not
    # sweet's layout: every core is Oryon, six "performance" ones in cluster 0
    # and two "prime" ones in cluster 1, and the MPIDRs are NOT core << 8.
    # Taken from the live device tree (cpu@0..cpu@500, cpu@10000, cpu@10100);
    # core << 8 would name 0x600 and 0x700, which this SoC does not have, so
    # xnu would ask PSCI to start two cores that do not exist. The same
    # mistake was in the shim's SMP sweep and hid the prime cores completely.
    # The kernel defaults to one core and uses cpus=N to select a prefix.
    # QEMU retains its original single-CPU tree: its MPIDRs differ from dodge.
    CPU_MPIDR = [0x00000, 0x00100, 0x00200, 0x00300,
                 0x00400, 0x00500, 0x10000, 0x10100]
    cpu_nodes = []
    for core in range(1 if qemu else 8):
        cluster = 0 if core < 6 else 1
        cpu_nodes.append(node(f'cpu{core}', [
            ph(),
            ('device_type', cstr('cpu')),
            ('compatible', cstr('qcom,oryon') + cstr('ARM,v8')),
            # Real MPIDR (Aff2:Aff1:Aff0), what PSCI CPU_ON is given, NOT a
            # logical ID. QEMU's single core is 0 either way.
            ('reg', u32(0 if qemu else CPU_MPIDR[core])),
            ('cpu-id', u32(core)),   # descriptive: XNU assigns IDs by DT order
            ('cluster-id', u32(cluster)),  # descriptive; see ml_parse_cpu_topology
            ('cluster-type', u32(0)),  # homogeneous CLUSTER_TYPE_SMP, not AMP
            ('die-cluster-id', u32(0)),
            ('cluster-core-id', u32(core if cluster == 0 else core - 6)),
            ('l2-cache-id', u32(core)),  # distinct per-core L2 nodes in live FDT
            ('l3-cache-id', u32(0)),     # shared L3
            ('state', cstr('running' if core == 0 else 'stopped')),
            ('timebase-frequency', u32(timebase)),
            ('clock-frequency', u32(2304000000 if core >= 6 else 1804800000)),
            ('bus-frequency', u32(100000000)),
            ('memory-frequency', u32(1866000000)),
            ('peripheral-frequency', u32(timebase)),
            ('fixed-frequency', u32(timebase)),
        ]))
    cpus = node('cpus', [ph(), ('#address-cells', u32(1)),
                         ('#size-cells', u32(0))], cpu_nodes)

    gic = node('gic', [
        ph(),
        ('compatible', cstr('arm,gic-v3')),
        ('interrupt-controller', b''),
        ('virtual-timer-intid', u32(QEMU_VTIMER_INTID if qemu else VTIMER_INTID)),
        ('reg', u64(gicd - SOC_BASE, GICD_SIZE, gicr - SOC_BASE, GICR_SIZE)),
    ])
    arm_io = node('arm-io', [
        ph(),
        ('device_type', cstr('dodge-io')),
        ('compatible', cstr('dodge-io') + cstr('arm-io')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
        ('ranges', u64(0, SOC_BASE, 0x100000000 - SOC_BASE)),
    ], [gic,
        # No hardware: watchdogd checks IODeviceTree:/arm-io/wdt for no-pmu and
        # otherwise crashes trying to set the PMU boot stage (customer builds
        # ignore the -wdt_no_pmu boot-arg).
        node('wdt', [ph(), ('no-pmu', u32(1))]),
        # Main display: IOMobileFramebuffer.framework counts /arm-io children
        # with this device_type and reads the canvas size from the node
        # (the presented iPhone X size, see DodgeFramebuffer.cpp);
        # the kernel's AppleCLCD matches the compatible string.
        node('disp0', [
            ph(),
            ('device_type', cstr('display-subsystem')),
            ('compatible', cstr('dodge,display')),
            ('canvas-width', u32(1125)),
            ('canvas-height', u32(2436)),
        ])])

    fbcon = node('dodge-fbcon', [
        ph(),
        ('reg', u64(FB, FB_SIZE)),
        ('width', u32(FB_W)),
        ('height', u32(FB_H)),
        ('stride', u32(FB_STRIDE)),
    ])

    # bsd/dev/dodge_ufs.c: the UFS host ABL leaves running (absolute physical reg).
    ufs = node('dodge-ufs', [
        ph(),
        ('compatible', cstr('qcom,ufshc')),
        ('reg', u64(UFS, UFS_SIZE)),
    ])

    # pmap_compute_max_asids requires the node even when no overrides are set.
    # An absent pmap-max-asids property selects XNU's own MAX_ASIDS default.
    # No effaceable storage: keybagd then reads /private/var/keybags/systembag.kb
    # as a plain plist instead of decrypting it with the effaceable key.
    defaults = node('defaults', [ph(), ('no-effaceable-storage', u32(1))])

    # MobileGestalt HomeButtonType = IODeviceTree:/buttons home-button-type
    # (D22AP: 2, no Home button; absent = 0 = Home-button device).
    buttons = node('buttons', [
        ph(),
        ('button-names', cstr('volup') + cstr('voldown') + cstr('hold')),
        ('home-button-type', u32(2)),
        ('opposed-power-vol-buttons', u32(1)),
    ])

    product_props = d22ap_product()
    product = [node('product', [ph()] + product_props)] if product_props else []

    # Identity of the iPhone X whose userland runs here: compatible[0] is
    # hw.model/hw.target (D22AP), model is hw.machine/hw.product
    # (iPhone10,3). 'oneplus,dodge' stays for DodgePlatformExpert matching.
    return node('device-tree', [
        ph(),
        ('compatible', cstr('D22AP') + cstr('iPhone10,3') + cstr('oneplus,dodge') + cstr('qcom,sm8750') + cstr('AppleARM')),
        ('model', cstr('iPhone10,3')),
        ('target-type', cstr('D22')),
        ('target-sub-type', cstr('D22AP')),
        ('manufacturer', cstr('Apple Inc.')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
    ], [chosen, defaults, cpus, arm_io, fbcon, buttons] + product + ([] if qemu else [ufs]))


if __name__ == '__main__':
    root = build(qemu='--qemu' in sys.argv, highmem='--highmem' in sys.argv)
    blob = adt.serialize(root)
    open(sys.argv[1], 'wb').write(blob)
    if '--dump' in sys.argv:
        adt.dump(adt.parse(blob)[0])
    print(f'{sys.argv[1]}: {len(blob)} bytes', file=sys.stderr)
