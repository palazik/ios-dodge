/*
 * Milestone 4: act as iBoot for xnu.
 *
 * mkpayload.py already resolved the kernel's chained fixups and built the
 * ADT, so this only has to: find the payload in the ABL ramdisk, copy the
 * segments to phys_base + (vmaddr - virt_base), place the ADT and
 * boot_args behind the kernel, and jump to the entry point with the MMU
 * and caches off, x0 = boot_args (physical).
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"
#include "loader.h"

#define BOOT_LINE_LENGTH	608
#define PAGE_16K		0x4000UL

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

/* pexpert/pexpert/arm64/boot.h, Revision/Version 2 */
struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

/* MMU off: everything is Device memory, so copy in aligned 8-byte words. */
static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

static int magic_ok(const char *m)
{
	static const char want[] = "DODGEXNU";
	for (int i = 0; i < 8; i++)
		if (m[i] != want[i])
			return 0;
	return 1;
}

typedef void (*xnu_entry_t)(uint64_t boot_args_pa);

const char *loader_boot(uintptr_t dtb)
{
	uint64_t rd_start = 0, rd_end = 0;

	/* Optional: the payload lives after _end, so an initrd is only a fallback. */
	if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) ||
	    !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end))
		rd_start = rd_end = 0;
	con_hex2("INITRD", "lo=", rd_start, "hi=", rd_end);

	/*
	 * Primary: the payload is appended directly after this image, so it sits
	 * at _end once ABL has copied the kernel blob. This is independent of how
	 * (or whether) the bootloader hands us a ramdisk -- a GKI/header-v4 boot
	 * builds its own initrd from vendor_boot+init_boot and can drop ours.
	 * Fallback: scan the initrd, in case ours was concatenated after theirs.
	 */
	extern char _end[];
	uintptr_t pbase = (uintptr_t)_end;

	if (!magic_ok(((const struct payload *)pbase)->magic)) {
		static const uint64_t want64 = 0x554e584547444f44ULL;	/* "DODGEXNU" */
		pbase = 0;
		for (uint64_t a = rd_start; a + sizeof(struct payload) <= rd_end; a += 8) {
			if (*(volatile const uint64_t *)(uintptr_t)a == want64) {
				pbase = (uintptr_t)a;
				break;
			}
		}
		if (!pbase)
			return "no DODGEXNU payload";
	}
	con_hex("PAYLOAD", pbase);

	const struct payload *p = (const struct payload *)pbase;
	if (p->version != 1 || (p->adt_off & 7) || (pbase & 7))
		return "bad payload header";

	uint64_t pa_of_va_base = p->phys_base - p->virt_base;	/* pa = va + this */
	con_hex2("PHYS", "base=", p->phys_base, "size=", p->mem_size);
	con_hex("VIRT", p->virt_base);
	con_hex("ENTRY", p->entry_va);

	/*
	 * The payload (our copy source) must not overlap where the kernel goes.
	 * Check pbase, not the initrd: the payload is appended after _end now and
	 * the bootloader's initrd is not ours at all.
	 */
	uint64_t payload_end = pbase + p->adt_off + p->adt_size;
	if (payload_end > p->phys_base && pbase < p->phys_base + p->mem_size)
		return "payload overlaps kernel memory";

	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + pa_of_va_base;
		copy64(dst, pbase + s->file_off, s->file_size);
		if (s->vmsize > s->file_size)
			zero64(dst + ((s->file_size + 7) & ~7UL), s->vmsize - ((s->file_size + 7) & ~7UL));
	}
	con_hex2("SEGS", "n=", p->nsegs, "end=", p->kernel_end + pa_of_va_base);

	/* ADT, then boot_args, behind the kernel; everything below topOfKernelData is ours. */
	uintptr_t adt_pa = p->kernel_end + pa_of_va_base;
	copy64(adt_pa, pbase + p->adt_off, p->adt_size);
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	uintptr_t top = ba_pa + PAGE_16K;

	zero64(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = FB_BASE;
	ba->Video.v_display = 1;
	ba->Video.v_rowBytes = FB_STRIDE;
	ba->Video.v_width = FB_W;
	ba->Video.v_height = FB_H;
	ba->Video.v_depth = 32;
	ba->deviceTreeP = adt_pa - pa_of_va_base;	/* xnu reads it with the MMU on: a VA */
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = p->mem_size;
	con_hex2("BOOTARG", "pa=", ba_pa, "top=", top);

	uintptr_t entry_pa = p->entry_va + pa_of_va_base;
	/*
	 * Read back what we actually landed at the entry. If the copy source is
	 * ever wrong again this shows garbage here instead of jumping into it and
	 * dying with an opaque "undefined instruction" panic.
	 */
	con_hex2("VERIFY", "entry=", entry_pa, "insn=", *(volatile const uint32_t *)entry_pa);
	con_hex("JUMP", entry_pa);
	con_str("XNU", "jumping (fbcon clears screen)");

	/*
	 * Hand over with the virtual timer disarmed and all interrupts masked.
	 * CNTV is a LEVEL interrupt: if ABL, or anything we did here, leaves it
	 * armed with its PPI pending, it storms the instant the kernel first
	 * unmasks DAIF - long before a timer driver exists to acknowledge it -
	 * and the symptom is a dead phone with no output, not an error.
	 *
	 * Found by blahajcoding/ios-lemonadep (commit 971286ce) on a OnePlus 9
	 * Pro, where the shim's own GIC test was what left it armed.
	 */
	__asm__ volatile("msr cntv_ctl_el0, xzr; msr daifset, #0xf; isb" ::: "memory");

	/* Everything was written with the MMU and caches off; drop any stale I-cache lines too. */
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	((xnu_entry_t)entry_pa)(ba_pa);
	return "xnu returned";
}
