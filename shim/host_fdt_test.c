/* Host test: run the shim's FDT walker over the OnePlus 13's live device tree. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "fdt.h"

int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror("open"); return 1; }
	fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	void *buf = malloc(n);
	if (fread(buf, 1, n, f) != (size_t)n) return 1;
	fclose(f);

	struct fdt_range bank[8], resv[96];
	int dyn = 0;
	int nb = fdt_ram_banks((uintptr_t)buf, bank, 8);
	int nr = fdt_reserved((uintptr_t)buf, resv, 96, &dyn);
	printf("banks=%d carveouts=%d dynamic=%d\n", nb, nr, dyn);
	for (int i = 0; i < nb && i < 8; i++)
		printf("  BANK %#llx size %#llx (end %#llx)\n",
		    (unsigned long long)bank[i].base, (unsigned long long)bank[i].size,
		    (unsigned long long)(bank[i].base + bank[i].size));

	if (nr > 96) nr = 96;
	for (int i = 1; i < nr; i++) {
		struct fdt_range k = resv[i]; int j = i - 1;
		while (j >= 0 && resv[j].base > k.base) { resv[j+1] = resv[j]; j--; }
		resv[j+1] = k;
	}
	const unsigned long long MINW = 256ull << 20;
	for (int b = 0; b < nb && b < 8; b++) {
		unsigned long long cur = bank[b].base, end = cur + bank[b].size;
		if (bank[b].size < MINW) continue;
		for (int i = 0; i < nr && cur < end; i++) {
			unsigned long long rb = resv[i].base, re = rb + resv[i].size;
			if (re <= cur || rb >= end) continue;
			if (rb > cur && rb - cur >= MINW)
				printf("  FREE %#llx .. %#llx  (%llu MiB)\n", cur, rb, (rb-cur)>>20);
			if (re > cur) cur = re;
		}
		if (cur < end && end - cur >= MINW)
			printf("  FREE %#llx .. %#llx  (%llu MiB)\n", cur, end, (end-cur)>>20);
	}
	/* Is the address mkadt.py hands xnu actually inside a free window? */
	unsigned long long pb = 0xc0000000ull, ms = 0x14000000ull;
	printf("\nPHYS_BASE %#llx size %#llx -> end %#llx\n", pb, ms, pb+ms);
	printf("32MB aligned: %s\n", (pb & 0x1ffffff) ? "NO" : "yes");
	for (int i = 0; i < nr; i++) {
		unsigned long long rb = resv[i].base, re = rb + resv[i].size;
		if (rb < pb + ms && re > pb)
			printf("  *** OVERLAPS carve-out %#llx..%#llx ***\n", rb, re);
	}
	return 0;
}
