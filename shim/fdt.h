#pragma once
#include <stdint.h>

/* Read a u32/u64 (cell-size agnostic) property of /chosen. Returns 0 if absent. */
int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out);

/*
 * RAM banks (/memory*) and reserved-memory carve-outs, read from the live FDT
 * ABL hands us. Values picked out of an offline dtb dump are a guess about the
 * phone in front of us; these are the phone in front of us. Each returns the
 * number of entries written, which may exceed max - the caller is told what it
 * did not see rather than silently getting a prefix.
 *
 * Idea from blahajcoding/ios-lemonadep (commits 1a31bd96, 982b2f02), which
 * reads geometry and carve-outs from the FDT instead of hardcoding them.
 */
struct fdt_range {
	uint64_t base, size;
};

int fdt_ram_banks(uintptr_t fdt, struct fdt_range *out, int max);
int fdt_reserved(uintptr_t fdt, struct fdt_range *out, int max, int *dynamic);
