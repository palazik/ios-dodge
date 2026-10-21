/*
 * Just enough flattened-device-tree parsing to find what ABL put in
 * /chosen (initrd location). Big-endian, 4-byte aligned tokens.
 */
#include "fdt.h"

#define FDT_MAGIC	0xd00dfeed
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_NOP		4
#define FDT_END		9

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static uintptr_t align4(uintptr_t a)
{
	return (a + 3) & ~3UL;
}

static uintptr_t skip_str(uintptr_t p)
{
	/* byte loads are fine on Device memory as long as they are aligned to 1 */
	while (*(volatile char *)p)
		p++;
	return p + 1;
}

int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	uintptr_t p = fdt + be32(fdt + 8);		/* off_dt_struct */
	uintptr_t strings = fdt + be32(fdt + 12);	/* off_dt_strings */
	int depth = 0, in_chosen = 0;

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			in_chosen = depth == 2 && streq(name, "chosen");
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (in_chosen)
				return 0;
			depth--;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			if (in_chosen && streq((const char *)(strings + nameoff), prop)) {
				if (len == 4)
					*out = be32(val);
				else if (len == 8)
					*out = (uint64_t)be32(val) << 32 | be32(val + 4);
				else
					return 0;
				return 1;
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		default:	/* FDT_END or garbage */
			return 0;
		}
	}
}

static uint64_t be64(uintptr_t a)
{
	return (uint64_t)be32(a) << 32 | be32(a + 4);
}

static int strpfx(const char *s, const char *pfx)
{
	while (*pfx && *s == *pfx)
		s++, pfx++;
	return *pfx == 0;
}

/*
 * One pass over the struct block, collecting "reg" from the nodes the caller
 * is after. #address-cells/#size-cells are 2/2 on every arm64 phone's /memory
 * and /reserved-memory, so a reg entry is a pair of 64-bit values; anything
 * else is skipped rather than guessed at.
 *
 * want_reserved = 0: nodes at depth 1 named memory*, i.e. the RAM banks.
 * want_reserved = 1: children of /reserved-memory, i.e. the carve-outs. Those
 * with no reg are dynamic pools that ABL places at boot; they are counted
 * through *dynamic instead, because their absence from the list is itself
 * information.
 */
static int fdt_collect(uintptr_t fdt, struct fdt_range *out, int max,
		       int want_reserved, int *dynamic)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	uintptr_t p = fdt + be32(fdt + 8);
	uintptr_t strings = fdt + be32(fdt + 12);
	int depth = 0, found = 0, in_resv_parent = 0, take = 0, has_reg = 0;

	if (dynamic)
		*dynamic = 0;

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			/* The root node is depth 1, so /memory and /reserved-memory
			 * are at depth 2 and the carve-outs at depth 3. */
			depth++;
			if (depth == 2)
				in_resv_parent = streq(name, "reserved-memory");
			if (want_reserved)
				take = depth == 3 && in_resv_parent;
			else
				take = depth == 2 &&
				    (streq(name, "memory") || strpfx(name, "memory@"));
			if (take)
				has_reg = 0;
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (take && want_reserved && !has_reg && dynamic)
				(*dynamic)++;
			take = 0;
			depth--;
			if (depth <= 0)
				return found;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			if (take && streq((const char *)(strings + nameoff), "reg") &&
			    len >= 16 && (len % 16) == 0) {
				has_reg = 1;
				for (uint32_t i = 0; i < len; i += 16) {
					if (found < max) {
						out[found].base = be64(val + i);
						out[found].size = be64(val + i + 8);
					}
					found++;
				}
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		default:
			return found;
		}
	}
}

int fdt_ram_banks(uintptr_t fdt, struct fdt_range *out, int max)
{
	return fdt_collect(fdt, out, max, 0, 0);
}

int fdt_reserved(uintptr_t fdt, struct fdt_range *out, int max, int *dynamic)
{
	return fdt_collect(fdt, out, max, 1, dynamic);
}
